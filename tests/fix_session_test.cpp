// Port of FixSessionTest.java: LOOPBACK session tests over 127.0.0.1 --
// logon handshake, order/exec-report round trip, heartbeats, TestRequest-
// on-silence escalation, sequence gap detection + resend recovery,
// duplicate suppression, too-low-seqnum disconnect, logout handshake, and
// a hard SequenceReset.
#include "quantfinlib/fix/fix_session.hpp"

#include <cmath>
#include <cstdint>
#include <future>
#include <limits>
#include <memory>
#include <string>
#include <thread>

#include "fix_test_helpers.hpp"
#include "qtest.hpp"
#include "quantfinlib/fix/execution_report.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/orderbook/side.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

using fix_test::CountdownFlag;
using fix_test::SyncList;
using quantfinlib::ExecutionReport;
using quantfinlib::FixMessage;
using quantfinlib::FixServerSocket;
using quantfinlib::FixSession;
using quantfinlib::NewOrderSingle;
using quantfinlib::Side;

namespace {

#ifdef _WIN32
// Raw-socket use in deadPeerEscalatesToTestRequestThenDisconnects needs
// Winsock initialized independently of (and possibly before) the
// FixSession library's own lazy WSAStartup.
struct WinsockInit {
    WinsockInit() {
        WSADATA data;
        WSAStartup(MAKEWORD(2, 2), &data);
    }
};
const WinsockInit kWinsockInit;
#endif

/** Collects callbacks with latches for the interesting events. */
class Recorder : public FixSession::Listener {
public:
    CountdownFlag logon;
    CountdownFlag logoutFlag;
    SyncList<ExecutionReport> reports;
    SyncList<NewOrderSingle> orders;
    SyncList<FixMessage> messages;
    std::atomic<std::int64_t> gapExpected{-1};
    std::atomic<std::int64_t> gapReceived{-1};
    CountdownFlag gap;
    CountdownFlag disconnected;
    CountdownFlag resendServed;
    std::atomic<std::int64_t> resendBegin{-1};
    std::atomic<std::int64_t> resendEnd{-1};

    void onLogon(FixSession&) override {
        logon.countDown();
    }

    void onLogout(FixSession&) override {
        logoutFlag.countDown();
    }

    void onExecutionReport(FixSession&, const ExecutionReport& report) override {
        reports.add(report);
    }

    void onNewOrderSingle(FixSession&, const NewOrderSingle& order) override {
        orders.add(order);
    }

    void onMessage(FixSession&, const FixMessage& message) override {
        messages.add(message);
    }

    void onSequenceGap(std::int64_t expected, std::int64_t received) override {
        gapExpected.store(expected);
        gapReceived.store(received);
        gap.countDown();
    }

    void onResendServed(std::int64_t beginSeqNo, std::int64_t endSeqNo) override {
        resendBegin.store(beginSeqNo);
        resendEnd.store(endSeqNo);
        resendServed.countDown();
    }

    void onDisconnect(const std::string& reason) override {
        {
            std::lock_guard<std::mutex> lock(reasonMutex_);
            disconnectReason_ = reason;
        }
        disconnected.countDown();
    }

    std::string reason() const {
        std::lock_guard<std::mutex> lock(reasonMutex_);
        return disconnectReason_;
    }

private:
    mutable std::mutex reasonMutex_;
    std::string disconnectReason_;
};

bool hasHeartbeatWithTestReqId(Recorder& r, const std::string& id) {
    for (auto& m : r.messages.snapshot()) {
        if (m.msgType() == FixMessage::HEARTBEAT && m.getString(FixMessage::TEST_REQ_ID, "") == id) {
            return true;
        }
    }
    return false;
}

struct SessionFixture {
    FixServerSocket server{0};
    Recorder clientEvents;
    Recorder venueEvents;
    std::unique_ptr<FixSession> client;
    std::unique_ptr<FixSession> venue;

    explicit SessionFixture(int heartbeatSeconds) {
        std::future<std::unique_ptr<FixSession>> accepting =
                std::async(std::launch::async, [&] {
                    return FixSession::accept(server, FixSession::Config("VENUE", "CLIENT", heartbeatSeconds),
                                              venueEvents);
                });
        client = FixSession::initiate("127.0.0.1", server.port(),
                                      FixSession::Config("CLIENT", "VENUE", heartbeatSeconds),
                                      clientEvents);
        venue = accepting.get();
    }
};

}  // namespace

TEST(logonHandshakeEstablishesBothSides) {
    SessionFixture f(30);
    EXPECT_TRUE(f.clientEvents.logon.await(5));
    EXPECT_TRUE(f.venueEvents.logon.await(5));
    EXPECT_TRUE(f.client->isEstablished());
    EXPECT_TRUE(f.venue->isEstablished());
}

TEST(orderFlowRoundTripsOverTheWire) {
    SessionFixture f(30);
    f.client->sendNewOrderSingle("ord-1", "EURUSD", Side::BUY, 1'000'000, 1.0851, NewOrderSingle::TIF_DAY);

    AWAIT_TRUE(f.venueEvents.orders.size() >= 1, "order not received");
    NewOrderSingle order = f.venueEvents.orders.first();
    EXPECT_TRUE(order.clOrdId == "ord-1");
    EXPECT_TRUE(order.symbol == "EURUSD");
    EXPECT_TRUE(order.side == Side::BUY);
    EXPECT_TRUE(order.quantity == 1'000'000);
    EXPECT_NEAR(order.price, 1.0851, 0.0);
    EXPECT_TRUE(!order.isMarket());

    f.venue->sendExecutionReport(ExecutionReport::accepted(order, "X-1", "E-1"));
    f.venue->sendExecutionReport(ExecutionReport::filled(order, "X-1", "E-2", 1.0851));

    AWAIT_TRUE(f.clientEvents.reports.size() >= 2, "execution reports not received");
    ExecutionReport ack = f.clientEvents.reports.get(0);
    ExecutionReport fill = f.clientEvents.reports.get(1);
    EXPECT_TRUE(ack.ordStatus == ExecutionReport::ORD_STATUS_NEW);
    EXPECT_TRUE(ack.clOrdId == "ord-1");
    EXPECT_TRUE(fill.isFill());
    EXPECT_TRUE(fill.ordStatus == ExecutionReport::ORD_STATUS_FILLED);
    EXPECT_NEAR(fill.lastPrice, 1.0851, 0.0);
    EXPECT_NEAR(fill.lastQty, 1'000'000, 0.0);
    EXPECT_NEAR(fill.leavesQty, 0, 0.0);
}

TEST(marketOrdersCarryNoPrice) {
    SessionFixture f(30);
    f.client->sendNewOrderSingle("mkt-1", "GBPUSD", Side::SELL, 500,
                                 std::numeric_limits<double>::quiet_NaN(), NewOrderSingle::TIF_IOC);
    AWAIT_TRUE(f.venueEvents.orders.size() >= 1, "order not received");
    NewOrderSingle order = f.venueEvents.orders.first();
    EXPECT_TRUE(order.isMarket());
    EXPECT_TRUE(std::isnan(order.price));
    EXPECT_TRUE(order.timeInForce == NewOrderSingle::TIF_IOC);
    EXPECT_TRUE(order.side == Side::SELL);
}

TEST(testRequestIsAnsweredWithMatchingHeartbeat) {
    SessionFixture f(30);
    f.venue->send(FixMessage::builder(FixMessage::TEST_REQUEST)
                          .field(FixMessage::TEST_REQ_ID, std::string("ping-42")));
    AWAIT_TRUE(hasHeartbeatWithTestReqId(f.venueEvents, "ping-42"), "no heartbeat reply with TestReqID");
}

TEST(heartbeatsFlowAndKeepTheSessionAlive) {
    SessionFixture f(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    EXPECT_TRUE(f.client->isEstablished());
    EXPECT_TRUE(f.venue->isEstablished());
    std::int64_t clientHeartbeats = 0;
    for (auto& m : f.clientEvents.messages.snapshot()) {
        if (m.msgType() == FixMessage::HEARTBEAT) {
            ++clientHeartbeats;
        }
    }
    EXPECT_TRUE(clientHeartbeats >= 1);
    // The UPPER bound matters too: heartbeating far above the negotiated 1s
    // interval is a venue-disconnect offense that "at least one" would
    // never catch. Generous (a slow CI cannot flake it) but an
    // interval-unit bug fails instantly.
    EXPECT_TRUE(clientHeartbeats <= 8);
}

TEST(deadPeerEscalatesToTestRequestThenDisconnects) {
    // A HALF-DEAD peer: TCP established and reading, application silent --
    // the failure mode that hangs sessions forever when the
    // TestRequest/timeout escalation regresses.
    FixServerSocket server(0);
    Recorder venueEvents;
    std::future<std::unique_ptr<FixSession>> accepting = std::async(std::launch::async, [&] {
        return FixSession::accept(server, FixSession::Config("VENUE", "CLIENT", 1), venueEvents);
    });

#ifdef _WIN32
    SOCKET raw = ::socket(AF_INET, SOCK_STREAM, 0);
#else
    int raw = ::socket(AF_INET, SOCK_STREAM, 0);
#endif
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<std::uint16_t>(server.port()));
    ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    ::connect(raw, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));

    std::vector<std::uint8_t> logonBytes =
            FixMessage::builder(FixMessage::LOGON)
                    .field(FixMessage::HEART_BT_INT, static_cast<std::int64_t>(1))
                    .encode("CLIENT", "VENUE", 1, "20260710-12:00:00.000");
    ::send(raw, reinterpret_cast<const char*>(logonBytes.data()),
          static_cast<int>(logonBytes.size()), 0);

    std::unique_ptr<FixSession> venue = accepting.get();
    EXPECT_TRUE(venue->isEstablished());

#ifdef _WIN32
    DWORD rcvTimeoutMs = 50;
    setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&rcvTimeoutMs),
              sizeof(rcvTimeoutMs));
#else
    timeval rcvTimeout{0, 50000};
    setsockopt(raw, SOL_SOCKET, SO_RCVTIMEO, &rcvTimeout, sizeof(rcvTimeout));
#endif

    // Read everything the venue sends (its writes never block), but never write again.
    std::string wire;
    char buf[1024];
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(8);
    while (std::chrono::steady_clock::now() < deadline && venueEvents.reason().empty()) {
        int n = ::recv(raw, buf, sizeof(buf), 0);
        if (n > 0) {
            wire.append(buf, static_cast<std::size_t>(n));
        }
    }
    // Final drain: on a stalled CI box the TestRequest and the disconnect
    // can land in the same heartbeat tick.
    int n;
    while ((n = ::recv(raw, buf, sizeof(buf), 0)) > 0) {
        wire.append(buf, static_cast<std::size_t>(n));
    }

    EXPECT_TRUE(wire.find("35=1") != std::string::npos);
    EXPECT_TRUE(!venueEvents.reason().empty());
    EXPECT_TRUE(!venue->isEstablished());

#ifdef _WIN32
    closesocket(raw);
#else
    ::close(raw);
#endif
}

TEST(sequenceGapsAreDetected) {
    SessionFixture f(30);
    f.venue->forceOutgoingSeq(50);
    f.venue->send(FixMessage::builder(FixMessage::HEARTBEAT));
    EXPECT_TRUE(f.clientEvents.gap.await(5));
    EXPECT_TRUE(f.clientEvents.gapReceived.load() == 50);
    EXPECT_TRUE(f.clientEvents.gapExpected.load() < 50);
    // Recovery: the heartbeat run is gap-filled and the sequence catches up.
    AWAIT_TRUE(f.client->expectedIncomingSeqNum() == 51, "gap not recovered");
}

TEST(logoutHandshakeClosesBothSidesCleanly) {
    SessionFixture f(30);
    f.client->logout();
    EXPECT_TRUE(f.venueEvents.logoutFlag.await(5));
    EXPECT_TRUE(f.clientEvents.logoutFlag.await(5));
    EXPECT_TRUE(!f.client->isEstablished());
    EXPECT_TRUE(!f.venue->isEstablished());
    EXPECT_THROWS(f.client->sendNewOrderSingle("x", "EURUSD", Side::BUY, 1, 1.0, '0'));
}

TEST(gapRecoveryRedeliversMissedApplicationMessages) {
    SessionFixture f(30);
    ExecutionReport report{"X-9", "E-9", ExecutionReport::EXEC_TYPE_TRADE,
                           ExecutionReport::ORD_STATUS_FILLED, "ord-9", "EURUSD",
                           Side::BUY, 100, 1.09, 100, 0, 1.09};

    // Venue jumps its outbound seq to 50: 2..49 never existed on the wire.
    f.venue->forceOutgoingSeq(50);
    f.venue->sendExecutionReport(report);

    // Client detects the gap, asks for a resend; venue gap-fills 2..49 and
    // replays the report at seq 50 with PossDup -- exactly one delivery.
    EXPECT_TRUE(f.clientEvents.gap.await(5));
    AWAIT_TRUE(f.clientEvents.reports.size() == 1, "report not recovered");
    EXPECT_TRUE(f.clientEvents.reports.first().clOrdId == "ord-9");
    EXPECT_TRUE(f.venueEvents.resendServed.await(5));
    EXPECT_TRUE(f.venueEvents.resendBegin.load() == 2);
    EXPECT_TRUE(f.venueEvents.resendEnd.load() == 50);
    AWAIT_TRUE(f.client->expectedIncomingSeqNum() == 51, "sequence not recovered");
    EXPECT_TRUE(f.client->isEstablished() && f.venue->isEstablished());

    // Session continues normally after recovery.
    f.venue->sendExecutionReport(report);
    AWAIT_TRUE(f.clientEvents.reports.size() == 2, "post-recovery message lost");
}

TEST(servicedResendsAreSuppressedAsDuplicatesByThePeer) {
    SessionFixture f(30);
    f.client->sendNewOrderSingle("ord-1", "EURUSD", Side::BUY, 100, 1.0851, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(f.venueEvents.orders.size() == 1, "order not received");

    // Venue explicitly asks for everything again.
    f.venue->send(FixMessage::builder(FixMessage::RESEND_REQUEST)
                          .field(FixMessage::BEGIN_SEQ_NO, static_cast<std::int64_t>(2))
                          .field(FixMessage::END_SEQ_NO, static_cast<std::int64_t>(0)));

    EXPECT_TRUE(f.clientEvents.resendServed.await(5));
    EXPECT_TRUE(f.clientEvents.resendBegin.load() == 2);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // let PossDup replay arrive
    // The replayed order is recognized as a duplicate, not a new order.
    EXPECT_TRUE(f.venueEvents.orders.size() == 1);
    EXPECT_TRUE(f.venue->isEstablished());
}

TEST(tooLowSequenceWithoutPossDupDisconnects) {
    SessionFixture f(30);
    f.venue->forceOutgoingSeq(1);  // repeats an already-used seqnum
    f.venue->send(FixMessage::builder(FixMessage::HEARTBEAT));

    EXPECT_TRUE(f.clientEvents.disconnected.await(5));
    EXPECT_TRUE(f.clientEvents.reason().find("too low") != std::string::npos);
    EXPECT_TRUE(!f.client->isEstablished());
}

TEST(hardSequenceResetJumpsExpectedSeqNum) {
    SessionFixture f(30);
    f.venue->send(FixMessage::builder(FixMessage::SEQUENCE_RESET)
                          .field(FixMessage::GAP_FILL_FLAG, std::string("N"))
                          .field(FixMessage::NEW_SEQ_NO, static_cast<std::int64_t>(100)));
    AWAIT_TRUE(f.client->expectedIncomingSeqNum() == 100, "reset not applied");
    EXPECT_TRUE(f.client->isEstablished());
}

QTEST_MAIN()
