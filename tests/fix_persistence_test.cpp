// Port of FixPersistenceTest.java: FileSessionStore round trip across a
// reopen, and full FixSession reconnect continuation -- sequence numbers
// resume, and a ResendRequest spanning the restart is serviced from the
// reopened file store with the pre-restart order correctly suppressed as
// a PossDup duplicate.
#include "quantfinlib/fix/file_session_store.hpp"
#include "quantfinlib/fix/fix_session.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <future>
#include <memory>
#include <string>

#include "fix_test_helpers.hpp"
#include "qtest.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/orderbook/side.hpp"

using fix_test::CountdownFlag;
using fix_test::SyncList;
using quantfinlib::FileSessionStore;
using quantfinlib::FixServerSocket;
using quantfinlib::FixSession;
using quantfinlib::NewOrderSingle;
using quantfinlib::Side;

namespace {

class Recorder : public FixSession::Listener {
public:
    SyncList<NewOrderSingle> orders;
    CountdownFlag resendServed;
    std::atomic<std::int64_t> gaps{0};

    void onNewOrderSingle(FixSession&, const NewOrderSingle& order) override {
        orders.add(order);
    }

    void onResendServed(std::int64_t, std::int64_t) override {
        resendServed.countDown();
    }

    void onSequenceGap(std::int64_t, std::int64_t) override {
        ++gaps;
    }
};

/** A fresh unique scratch directory, removed at scope exit. */
struct TempDir {
    std::filesystem::path path;

    TempDir() {
        auto base = std::filesystem::temp_directory_path();
        for (int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base / ("qfl_fix_test_" + std::to_string(std::rand()) + "_"
                                     + std::to_string(attempt));
            std::error_code ec;
            if (std::filesystem::create_directories(candidate, ec)) {
                path = candidate;
                return;
            }
        }
        throw std::runtime_error("failed to create a unique temp dir");
    }

    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

}  // namespace

TEST(fileStoreRoundTripsAcrossReopen) {
    TempDir dir;
    {
        FileSessionStore store(dir.path.string());
        EXPECT_TRUE(store.nextOutgoingSeq() == 1);
        EXPECT_TRUE(store.expectedIncomingSeq() == 1);
        store.saveOutgoingSeq(7);
        store.saveIncomingSeq(4);
        store.storeMessage(2, quantfinlib::FixSessionStore::StoredMessage{
                                       "D", "11=ord-155=EURUSD", "20260704-10:00:00.000"});
        store.storeMessage(5, quantfinlib::FixSessionStore::StoredMessage{
                                       "8", "37=X-1", "20260704-10:00:01.000"});
    }
    {
        FileSessionStore reopened(dir.path.string());
        EXPECT_TRUE(reopened.nextOutgoingSeq() == 7);
        EXPECT_TRUE(reopened.expectedIncomingSeq() == 4);
        auto m2 = reopened.retrieve(2);
        EXPECT_TRUE(m2.has_value());
        EXPECT_TRUE(m2->msgType == "D");
        EXPECT_TRUE(m2->body.find("11=ord-1") != std::string::npos);
        auto m5 = reopened.retrieve(5);
        EXPECT_TRUE(m5.has_value());
        EXPECT_TRUE(m5->msgType == "8");
        EXPECT_TRUE(!reopened.retrieve(3).has_value());
    }
}

TEST(sessionsContinueSequenceNumbersAcrossReconnect) {
    TempDir dir;
    std::filesystem::path clientDir = dir.path / "client";
    std::filesystem::path venueDir = dir.path / "venue";
    FixSession::Config clientCfg("CLIENT", "VENUE", 30);
    FixSession::Config venueCfg("VENUE", "CLIENT", 30);

    FixServerSocket server(0);

    // ---- Session 1: logon, one order, clean logout ----
    Recorder venueEvents1;
    std::future<std::unique_ptr<FixSession>> accepting1 = std::async(std::launch::async, [&] {
        return FixSession::accept(server, venueCfg, venueEvents1,
                                  std::make_shared<FileSessionStore>(venueDir.string()));
    });
    Recorder clientEvents1;
    std::unique_ptr<FixSession> client1 = FixSession::initiate(
            "127.0.0.1", server.port(), clientCfg, clientEvents1,
            std::make_shared<FileSessionStore>(clientDir.string()));
    std::unique_ptr<FixSession> venue1 = accepting1.get();

    client1->sendNewOrderSingle("ord-1", "EURUSD", Side::BUY, 100, 1.0851, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(venueEvents1.orders.size() == 1, "order not received");
    client1->logout();
    venue1->close();
    client1.reset();
    venue1.reset();

    // ---- Session 2: same stores, fresh sessions ----
    Recorder venueEvents2;
    Recorder clientEvents2;
    std::future<std::unique_ptr<FixSession>> accepting2 = std::async(std::launch::async, [&] {
        return FixSession::accept(server, venueCfg, venueEvents2,
                                  std::make_shared<FileSessionStore>(venueDir.string()));
    });
    std::unique_ptr<FixSession> client2 = FixSession::initiate(
            "127.0.0.1", server.port(), clientCfg, clientEvents2,
            std::make_shared<FileSessionStore>(clientDir.string()));
    std::unique_ptr<FixSession> venue2 = accepting2.get();

    // Continuation: no gaps, both established, seqs resumed above 1.
    EXPECT_TRUE(client2->isEstablished() && venue2->isEstablished());
    EXPECT_TRUE(clientEvents2.gaps.load() == 0);
    EXPECT_TRUE(venueEvents2.gaps.load() == 0);
    EXPECT_TRUE(venue2->expectedIncomingSeqNum() > 2);

    // New order flows on the continued sequence.
    client2->sendNewOrderSingle("ord-2", "EURUSD", Side::SELL, 50, 1.0860, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(venueEvents2.orders.size() == 1, "post-reconnect order lost");
    EXPECT_TRUE(venueEvents2.orders.first().clOrdId == "ord-2");

    // Resend across the restart: ord-1 (from session 1!) is replayed from
    // the reopened file store and correctly suppressed as PossDup.
    venue2->send(quantfinlib::FixMessage::builder(quantfinlib::FixMessage::RESEND_REQUEST)
                         .field(quantfinlib::FixMessage::BEGIN_SEQ_NO, static_cast<std::int64_t>(2))
                         .field(quantfinlib::FixMessage::END_SEQ_NO, static_cast<std::int64_t>(0)));
    EXPECT_TRUE(clientEvents2.resendServed.await(5));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    EXPECT_TRUE(venueEvents2.orders.size() == 1);
    EXPECT_TRUE(venue2->isEstablished());

    client2->logout();
    venue2->close();
}

QTEST_MAIN()
