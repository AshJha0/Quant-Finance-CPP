// Regression tests for the 2026-11-06 review, findings C6-C10 and H6/H9. A
// raw socket plays the counterparty, because a well-behaved FixSession peer
// can never produce the bytes these rules exist to stop.
#include "qtest.hpp"

#ifdef _WIN32
QTEST_MAIN()
#else

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <future>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "fix_test_helpers.hpp"
#include "quantfinlib/fix/fix_session.hpp"
#include "quantfinlib/fix/fix_session_store.hpp"

using fix_test::SyncList;
using quantfinlib::FixMessage;
using quantfinlib::FixServerSocket;
using quantfinlib::FixSession;
using quantfinlib::FixSessionStore;
using quantfinlib::InMemorySessionStore;
using quantfinlib::NewOrderSingle;
using quantfinlib::Side;

namespace {

constexpr const char* TS = "20261106-10:00:00.000";

class Recorder : public FixSession::Listener {
public:
    std::atomic<int> orders{0};
    std::atomic<int> disconnects{0};
    std::atomic<bool> refuseLogon{false};
    SyncList<std::string> reasons;
    SyncList<std::string> events;

    bool authenticateLogon(const FixMessage&) override {
        return !refuseLogon.load();
    }
    void onNewOrderSingle(FixSession&, const NewOrderSingle&) override {
        ++orders;
        events.add("dispatch");
    }
    void onDisconnect(const std::string& reason) override {
        reasons.add(reason);
        ++disconnects;
    }
};

/** Records when the session persists relative to the application callback. */
class OrderingStore : public InMemorySessionStore {
public:
    explicit OrderingStore(Recorder& r) : recorder_(r) {}
    void saveIncomingSeq(std::int64_t seq) override {
        InMemorySessionStore::saveIncomingSeq(seq);
        recorder_.events.add("persist " + std::to_string(seq));
    }

private:
    Recorder& recorder_;
};

/** The counterparty end of a TCP connection, speaking raw FIX. */
class RawPeer {
public:
    RawPeer() {
        listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        ::bind(listener_, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        ::listen(listener_, 4);
        socklen_t len = sizeof(a);
        ::getsockname(listener_, reinterpret_cast<sockaddr*>(&a), &len);
        port = ntohs(a.sin_port);
    }
    ~RawPeer() {
        if (fd >= 0) {
            ::close(fd);
        }
        ::close(listener_);
    }

    /** Takes the session's Logon and answers it, leaving the peer's next seq at 2. */
    void acceptAndLogon(const std::string& extra = "") {
        fd = ::accept(listener_, nullptr, nullptr);
        char buf[512];
        (void)::recv(fd, buf, sizeof(buf), 0);
        FixMessage::Builder b = logon();
        if (!extra.empty()) {
            b.field(FixMessage::RESET_SEQ_NUM_FLAG, extra);
        }
        send(b, 1);
    }

    void dial(int targetPort) {
        fd = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        a.sin_port = htons(static_cast<std::uint16_t>(targetPort));
        ::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    }

    static FixMessage::Builder logon() {
        return FixMessage::builder(FixMessage::LOGON)
                .field(FixMessage::ENCRYPT_METHOD, std::string("0"))
                .field(FixMessage::HEART_BT_INT, static_cast<std::int64_t>(30));
    }

    static FixMessage::Builder order() {
        return FixMessage::builder(FixMessage::NEW_ORDER_SINGLE)
                .field(FixMessage::CL_ORD_ID, std::string("1"))
                .field(FixMessage::SYMBOL, std::string("X"))
                .field(FixMessage::SIDE, '1')
                .field(FixMessage::TRANSACT_TIME, std::string(TS))
                .field(FixMessage::ORDER_QTY, static_cast<std::int64_t>(1))
                .field(FixMessage::ORD_TYPE, '1')
                .field(FixMessage::TIME_IN_FORCE, '0');
    }

    static FixMessage::Builder gapFill(std::int64_t newSeq) {
        return FixMessage::builder(FixMessage::SEQUENCE_RESET)
                .field(FixMessage::GAP_FILL_FLAG, std::string("Y"))
                .field(FixMessage::NEW_SEQ_NO, newSeq);
    }

    static FixMessage::Builder resend(std::int64_t begin, std::int64_t end) {
        return FixMessage::builder(FixMessage::RESEND_REQUEST)
                .field(FixMessage::BEGIN_SEQ_NO, begin)
                .field(FixMessage::END_SEQ_NO, end);
    }

    void send(const FixMessage::Builder& b, std::int64_t seq, const std::string& sender = "EX",
              const std::string& target = "ME") {
        raw(b.encode(sender, target, seq, TS));
    }

    void sendPossDup(const FixMessage::Builder& b, std::int64_t seq) {
        raw(b.encode("EX", "ME", seq, TS, true, std::string(TS)));
    }

    void raw(const std::vector<std::uint8_t>& bytes) {
        (void)::send(fd, bytes.data(), bytes.size(), MSG_NOSIGNAL);
    }

    /** Everything readable for `ms`, SOH shown as '|'. */
    std::string drain(int ms) {
        timeval tv{0, ms * 1000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        std::string out;
        char buf[8192];
        ssize_t n;
        while ((n = ::recv(fd, buf, sizeof(buf), 0)) > 0) {
            out.append(buf, static_cast<std::size_t>(n));
        }
        eof = n == 0;
        for (char& c : out) {
            if (c == 1) {
                c = '|';
            }
        }
        return out;
    }

    int port = 0;
    int fd = -1;
    bool eof = false;

private:
    int listener_ = -1;
};

FixSession::Config config(int heartbeat = 30) {
    return FixSession::Config("ME", "EX", heartbeat);
}

std::unique_ptr<FixSession> connect(RawPeer& peer, Recorder& r, const FixSession::Config& c,
                                    std::shared_ptr<FixSessionStore> store = nullptr,
                                    const std::string& logonExtra = "") {
    std::thread venue([&] { peer.acceptAndLogon(logonExtra); });
    auto s = store ? FixSession::initiate("127.0.0.1", peer.port, c, r, store)
                   : FixSession::initiate("127.0.0.1", peer.port, c, r);
    venue.join();
    peer.drain(20);
    return s;
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

std::int64_t elapsedMs(std::chrono::steady_clock::time_point from) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - from)
            .count();
}

}  // namespace

// ---------------------------------------------------------------- C6

TEST(closeSendsFinAndTheDestructorDoesNotWaitForThePeer) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    auto start = std::chrono::steady_clock::now();
    s->close();
    s->close();  // close-once: the second call must be harmless
    peer.drain(200);
    EXPECT_TRUE(peer.eof);
    s.reset();
    EXPECT_TRUE(elapsedMs(start) < 2000);
}

TEST(aSendToAClosedPeerIsAnErrorNotASignal) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    ::close(peer.fd);
    peer.fd = -1;
    // Without MSG_NOSIGNAL the second write kills this test process.
    for (int i = 0; i < 50 && r.disconnects.load() == 0; ++i) {
        s->send(FixMessage::builder(FixMessage::HEARTBEAT));
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    AWAIT_TRUE(r.disconnects.load() == 1, "no disconnect after peer close");
}

TEST(concurrentCloseFromManyThreadsIsSafe) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    std::vector<std::thread> closers;
    for (int i = 0; i < 4; ++i) {
        closers.emplace_back([&] { s->close(); });
    }
    for (auto& t : closers) {
        t.join();
    }
    EXPECT_TRUE(!s->isEstablished());
}

// ---------------------------------------------------------------- C7

TEST(aStalledPeerCannotHoldTheSenderOrTheWatchdog) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config(1).withSendTimeout(200));
    auto start = std::chrono::steady_clock::now();
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        std::string big(4000, 'x');
        while (!stop.load() && s->isEstablished()) {
            s->send(FixMessage::builder("Z").field(FixMessage::TEXT, big));
        }
    });
    AWAIT_TRUE(r.disconnects.load() == 1, "stalled peer never detected");
    stop.store(true);
    writer.join();
    EXPECT_TRUE(!s->isEstablished());
    EXPECT_TRUE(elapsedMs(start) < 4000);
}

// ---------------------------------------------------------------- C8

TEST(anApplicationMessageBeforeLogonNeverReachesTheListener) {
    FixServerSocket server(0);
    Recorder r;
    RawPeer peer;
    std::thread client([&] {
        peer.dial(server.port());
        peer.send(RawPeer::order(), 1, "EX", "ME");
    });
    bool threw = false;
    try {
        FixSession::accept(server, config(), r);
    } catch (const std::runtime_error& e) {
        threw = contains(e.what(), "first message must be Logon");
    }
    client.join();
    EXPECT_TRUE(threw);
    EXPECT_TRUE(r.orders.load() == 0);
}

TEST(aLogonWithTheWrongCompIdsIsRefusedWithoutReply) {
    FixServerSocket server(0);
    Recorder r;
    RawPeer peer;
    std::thread client([&] {
        peer.dial(server.port());
        peer.send(RawPeer::logon(), 1, "EVIL", "NOBODY");
    });
    bool threw = false;
    try {
        FixSession::accept(server, config(), r);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    client.join();
    EXPECT_TRUE(threw);
    EXPECT_TRUE(!contains(peer.drain(50), "35=A"));
}

TEST(aRefusedLogonCannotResetTheStore) {
    FixServerSocket server(0);
    Recorder r;
    r.refuseLogon.store(true);
    auto store = std::make_shared<InMemorySessionStore>();
    store->saveOutgoingSeq(7);
    RawPeer peer;
    std::thread client([&] {
        peer.dial(server.port());
        peer.send(RawPeer::logon().field(FixMessage::RESET_SEQ_NUM_FLAG, std::string("Y")), 1);
    });
    bool threw = false;
    try {
        FixSession::accept(server, config(), r, store);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    client.join();
    EXPECT_TRUE(threw);
    EXPECT_TRUE(store->nextOutgoingSeq() == 7);
}

TEST(aWrongCompIdAfterLogonEndsTheSessionUndelivered) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::order(), 2, "EVIL", "ME");
    AWAIT_TRUE(r.disconnects.load() == 1, "wrong CompID accepted");
    EXPECT_TRUE(r.orders.load() == 0);
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 2);
    EXPECT_TRUE(contains(peer.drain(50), "35=5"));
}

TEST(acceptThrowsWhenThePeerDiesBeforeLogon) {
    FixServerSocket server(0);
    Recorder r;
    RawPeer peer;
    std::thread client([&] {
        peer.dial(server.port());
        ::close(peer.fd);
        peer.fd = -1;
    });
    bool threw = false;
    try {
        FixSession::accept(server, config(), r);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    client.join();
    EXPECT_TRUE(threw);
}

// ---------------------------------------------------------------- C9

TEST(aGapFillAboveExpectedIsAGapNotAJump) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::gapFill(100), 50);
    std::string back = peer.drain(100);
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 2);
    EXPECT_TRUE(contains(back, "35=2"));
    EXPECT_TRUE(contains(back, "7=2|"));
}

TEST(aGapFillBelowExpectedWithoutPossDupDisconnects) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::order(), 2);
    peer.send(RawPeer::gapFill(100), 1);
    AWAIT_TRUE(r.disconnects.load() == 1, "too-low GapFill accepted");
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 3);
}

TEST(anUnsolicitedGapFillIsRejected) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.sendPossDup(RawPeer::gapFill(100), 2);
    std::string back = peer.drain(100);
    EXPECT_TRUE(contains(back, "35=3"));
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 2);
}

TEST(aGapFillInsideAResendAdvancesAndALoweringOneDoesNot) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::order(), 5);  // gap: 2-4 missing
    peer.drain(50);
    peer.sendPossDup(RawPeer::gapFill(2), 2);  // NewSeqNo must exceed MsgSeqNum
    EXPECT_TRUE(contains(peer.drain(50), "35=3"));
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 2);
    peer.sendPossDup(RawPeer::gapFill(5), 2);
    peer.sendPossDup(RawPeer::order(), 5);
    AWAIT_TRUE(r.orders.load() == 1, "replayed order lost");
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 6);
}

TEST(aTrailingGapFillAfterTheTriggerSeqStillCounts) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::order(), 3);  // gap on 2, trigger 3
    peer.drain(50);
    peer.sendPossDup(RawPeer::order(), 2);
    peer.sendPossDup(RawPeer::order(), 3);
    peer.sendPossDup(RawPeer::gapFill(9), 4);  // admin run after the trigger
    AWAIT_TRUE(s->expectedIncomingSeqNum() == 9, "trailing GapFill refused");
    peer.send(RawPeer::order(), 9);
    AWAIT_TRUE(r.orders.load() == 3, "live order lost");
    peer.sendPossDup(RawPeer::gapFill(20), 10);  // live traffic closed the window
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 10);
}

TEST(sequenceResetModeIsRefusedUnlessConfigured) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(FixMessage::builder(FixMessage::SEQUENCE_RESET)
                      .field(FixMessage::NEW_SEQ_NO, static_cast<std::int64_t>(500)),
              1);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 2);
    EXPECT_TRUE(s->isEstablished());
}

TEST(sequenceResetModeFollowsTheSpecWhenEnabled) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config().withSequenceResetMode());
    auto reset = [](std::int64_t n) {
        return FixMessage::builder(FixMessage::SEQUENCE_RESET).field(FixMessage::NEW_SEQ_NO, n);
    };
    peer.send(reset(500), 1);  // MsgSeqNum is ignored in Reset mode
    AWAIT_TRUE(s->expectedIncomingSeqNum() == 500, "reset ignored");
    peer.send(reset(10), 1);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 500);
}

// ---------------------------------------------------------------- C10

TEST(resetSeqNumClearsTheStoreSoOldOrdersCannotReplay) {
    RawPeer peer;
    Recorder r;
    auto store = std::make_shared<InMemorySessionStore>();
    store->storeMessage(2, {"D", "11=OLD\x01", "20991231-23:59:59.000"});
    store->saveOutgoingSeq(3);
    auto s = connect(peer, r, config().withResetOnLogon(), store, "Y");
    EXPECT_TRUE(!store->retrieve(2).has_value());
    s->send(FixMessage::builder(FixMessage::HEARTBEAT));  // 34=2 is now admin
    peer.send(RawPeer::resend(1, 0), 2);
    EXPECT_TRUE(!contains(peer.drain(100), "11=OLD"));
}

TEST(aPeerInitiatedResetAlsoClearsTheStore) {
    RawPeer peer;
    Recorder r;
    auto store = std::make_shared<InMemorySessionStore>();
    store->storeMessage(2, {"D", "11=OLD\x01", "20991231-23:59:59.000"});
    store->saveOutgoingSeq(3);
    auto s = connect(peer, r, config(), store, "Y");
    EXPECT_TRUE(!store->retrieve(2).has_value());
    EXPECT_TRUE(store->nextOutgoingSeq() == 1);
}

TEST(aResetFlagOnASequenceOtherThanOneIsRefused) {
    Recorder r;
    auto store = std::make_shared<InMemorySessionStore>();
    store->storeMessage(1, {"D", "x", TS});
    store->saveOutgoingSeq(2);
    FixServerSocket server(0);
    RawPeer client;
    std::thread dialer([&] {
        client.dial(server.port());
        client.send(RawPeer::logon().field(FixMessage::RESET_SEQ_NUM_FLAG, std::string("Y")), 5);
    });
    bool threw = false;
    try {
        FixSession::accept(server, config(), r, store);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    dialer.join();
    EXPECT_TRUE(threw);
    EXPECT_TRUE(store->retrieve(1).has_value());
}

TEST(staleOrdersAreGapFilledButFreshOnesAndFillsAreReplayed) {
    RawPeer peer;
    Recorder r;
    auto store = std::make_shared<InMemorySessionStore>();
    store->storeMessage(2, {"D", "11=STALE\x01", "20200101-10:00:00.000"});
    store->storeMessage(3, {"8", "17=OLDFILL\x01", "20200101-10:00:00.000"});
    store->storeMessage(4, {"D", "11=BADTIME\x01", "garbage"});
    store->saveOutgoingSeq(5);
    auto s = connect(peer, r, config(), store);
    s->sendNewOrderSingle("FRESH", "X", Side::BUY, 1, 1.0, '0');
    peer.drain(20);
    peer.send(RawPeer::resend(2, 0), 2);
    std::string back = peer.drain(100);
    EXPECT_TRUE(!contains(back, "11=STALE"));
    EXPECT_TRUE(!contains(back, "11=BADTIME"));
    EXPECT_TRUE(contains(back, "17=OLDFILL"));
    EXPECT_TRUE(contains(back, "11=FRESH"));
    EXPECT_TRUE(contains(back, "123=Y"));
}

TEST(hostileResendRangesAreRejectedAndServeNothing) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(RawPeer::resend(-9000000000000000000LL, 0), 2);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    peer.send(RawPeer::resend(5, 2), 3);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    peer.send(RawPeer::resend(1000, 0), 4);  // beyond anything sent
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    peer.send(RawPeer::resend(1, -1), 5);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    EXPECT_TRUE(s->isEstablished());
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 6);
}

// ---------------------------------------------------------------- H6

TEST(inboundSequenceIsPersistedOnlyAfterDispatch) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config(), std::make_shared<OrderingStore>(r));
    peer.send(RawPeer::order(), 2);
    AWAIT_TRUE(r.events.size() >= 3, "order not processed");
    std::vector<std::string> events = r.events.snapshot();
    EXPECT_TRUE(events[events.size() - 2] == "dispatch");
    EXPECT_TRUE(events.back() == "persist 3");
}

// ---------------------------------------------------------------- H9

TEST(aGarbledFrameIsIgnoredAndRecoveredAsAGap) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    std::vector<std::uint8_t> bad = RawPeer::order().encode("EX", "ME", 2, TS);
    bad[bad.size() - 2] = bad[bad.size() - 2] == '0' ? '1' : '0';  // break the checksum
    peer.raw(bad);
    peer.send(RawPeer::order(), 3);
    EXPECT_TRUE(contains(peer.drain(100), "35=2"));
    EXPECT_TRUE(s->isEstablished());
    EXPECT_TRUE(r.orders.load() == 0);
}

TEST(aStreamOfGarbledFramesEndsTheSession) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    std::vector<std::uint8_t> bad = RawPeer::order().encode("EX", "ME", 2, TS);
    bad[bad.size() - 2] = bad[bad.size() - 2] == '0' ? '1' : '0';
    for (int i = 0; i < 20; ++i) {
        peer.raw(bad);
    }
    AWAIT_TRUE(r.disconnects.load() == 1, "garbage kept the session alive");
}

TEST(aBadRequiredFieldOnAnAdminMessageIsRejectedNotFatal) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config());
    peer.send(FixMessage::builder(FixMessage::RESEND_REQUEST)
                      .field(FixMessage::BEGIN_SEQ_NO, std::string("abc"))
                      .field(FixMessage::END_SEQ_NO, std::string("0")),
              2);
    EXPECT_TRUE(contains(peer.drain(100), "35=3"));
    EXPECT_TRUE(s->isEstablished());
    EXPECT_TRUE(s->expectedIncomingSeqNum() == 3);
}

TEST(testRequestIsSatisfiedOnlyByItsOwnId) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config(1));
    std::string probe;
    for (int i = 0; i < 30 && !contains(probe, "35=1"); ++i) {
        probe += peer.drain(100);
    }
    EXPECT_TRUE(contains(probe, "35=1"));
    std::size_t at = probe.find("112=");
    std::string id = probe.substr(at + 4, probe.find('|', at) - at - 4);
    EXPECT_TRUE(s->testRequestOutstanding());
    peer.send(FixMessage::builder(FixMessage::HEARTBEAT)
                      .field(FixMessage::TEST_REQ_ID, std::string("wrong")),
              2);
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    EXPECT_TRUE(s->testRequestOutstanding());
    peer.send(FixMessage::builder(FixMessage::HEARTBEAT).field(FixMessage::TEST_REQ_ID, id), 3);
    AWAIT_TRUE(!s->testRequestOutstanding(), "matching heartbeat ignored");
}

TEST(junkBytesDoNotCountAsLife) {
    RawPeer peer;
    Recorder r;
    auto s = connect(peer, r, config(1));
    // Frame start only: never completes, so nothing valid ever arrives.
    std::vector<std::uint8_t> head{'8', '=', 'F', 'I', 'X', '.', '4', '.', '4', 1, '9', '=', '9',
                                   '9', '9', 1};
    peer.raw(head);
    std::vector<std::uint8_t> dribble(1, 'x');
    for (int i = 0; i < 40 && r.disconnects.load() == 0; ++i) {
        peer.raw(dribble);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    EXPECT_TRUE(r.disconnects.load() == 1);
    EXPECT_TRUE(contains(r.reasons.first(), "heartbeat timeout"));
}

TEST(aLogonSizedLimitAppliesBeforeLogon) {
    FixServerSocket server(0);
    Recorder r;
    RawPeer peer;
    std::thread client([&] {
        peer.dial(server.port());
        std::string head = std::string("8=FIX.4.4\x01") + "9=1000000\x01";
        peer.raw(std::vector<std::uint8_t>(head.begin(), head.end()));
    });
    auto start = std::chrono::steady_clock::now();
    bool threw = false;
    try {
        FixSession::accept(server, config(), r);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    client.join();
    EXPECT_TRUE(threw);
    EXPECT_TRUE(elapsedMs(start) < 2000);  // refused on the header, not by the logon timeout
}

QTEST_MAIN()

#endif  // _WIN32
