// Port of FixResetSeqNumTest.java: ResetSeqNumFlag(141), the clean-slate
// reconnect. The scenario that matters is a fresh store on one side against
// a durable store on the other -- without 141=Y the fresh peer's seq 1 is
// "too low" and the session dies on sight (proved by the control test);
// with it, both sides restart at 1 and trade flows with no resend storm
// and no disconnect.
#include "quantfinlib/fix/fix_session.hpp"

#include <future>
#include <memory>
#include <string>

#include "fix_test_helpers.hpp"
#include "qtest.hpp"
#include "quantfinlib/fix/execution_report.hpp"
#include "quantfinlib/fix/fix_session_store.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/orderbook/side.hpp"

using fix_test::CountdownFlag;
using fix_test::SyncList;
using quantfinlib::ExecutionReport;
using quantfinlib::FixServerSocket;
using quantfinlib::FixSession;
using quantfinlib::FixSessionStore;
using quantfinlib::InMemorySessionStore;
using quantfinlib::NewOrderSingle;
using quantfinlib::Side;

namespace {

class Recorder : public FixSession::Listener {
public:
    CountdownFlag logon;
    SyncList<NewOrderSingle> orders;
    SyncList<ExecutionReport> reports;
    std::atomic<std::int64_t> gapExpected{-1};
    CountdownFlag disconnected;

    void onLogon(FixSession&) override {
        logon.countDown();
    }

    void onNewOrderSingle(FixSession&, const NewOrderSingle& order) override {
        orders.add(order);
    }

    void onExecutionReport(FixSession&, const ExecutionReport& report) override {
        reports.add(report);
    }

    void onSequenceGap(std::int64_t expected, std::int64_t) override {
        gapExpected.store(expected);
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

/** A venue store left behind by a long-lived previous session. */
std::shared_ptr<FixSessionStore> staleStore() {
    auto store = std::make_shared<InMemorySessionStore>();
    store->saveOutgoingSeq(55);
    store->saveIncomingSeq(55);
    return store;
}

}  // namespace

TEST(resetOnLogonReconnectsAFreshStoreAgainstAStaleOneWithoutDisconnect) {
    FixServerSocket server(0);
    Recorder clientEvents;
    Recorder venueEvents;
    std::shared_ptr<FixSessionStore> venueStore = staleStore();

    std::future<std::unique_ptr<FixSession>> accepting = std::async(std::launch::async, [&] {
        return FixSession::accept(server, FixSession::Config("VENUE", "CLIENT", 30), venueEvents,
                                  venueStore);
    });
    // Fresh in-memory store on the client + 141=Y: both sides restart at 1.
    std::unique_ptr<FixSession> client = FixSession::initiate(
            "127.0.0.1", server.port(),
            FixSession::Config("CLIENT", "VENUE", 30).withResetOnLogon(), clientEvents);
    std::unique_ptr<FixSession> venue = accepting.get();

    EXPECT_TRUE(clientEvents.logon.await(5));
    EXPECT_TRUE(venueEvents.logon.await(5));
    EXPECT_TRUE(client->isEstablished() && venue->isEstablished());

    // Trade flows on the reset numbering: order out, report back.
    client->sendNewOrderSingle("rst-1", "EURUSD", Side::BUY, 100, 1.0851, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(venueEvents.orders.size() == 1, "order not received");
    EXPECT_TRUE(venueEvents.orders.first().clOrdId == "rst-1");
    venue->sendExecutionReport(ExecutionReport::accepted(venueEvents.orders.first(), "X-1", "E-1"));
    AWAIT_TRUE(clientEvents.reports.size() == 1, "report not received");

    // The reset actually took: logon was 1, order was 2 -- the venue now
    // expects 3, nowhere near the stale 55; ditto the client after the
    // venue's logon (1) and report (2). No gaps, no disconnects.
    EXPECT_TRUE(venue->expectedIncomingSeqNum() == 3);
    EXPECT_TRUE(client->expectedIncomingSeqNum() == 3);
    EXPECT_TRUE(clientEvents.gapExpected.load() == -1);
    EXPECT_TRUE(venueEvents.gapExpected.load() == -1);
    EXPECT_TRUE(venueEvents.reason().empty());
    EXPECT_TRUE(clientEvents.reason().empty());
    // And the venue's durable store carries the reset numbering forward.
    EXPECT_TRUE(venueStore->nextOutgoingSeq() == 3);
    EXPECT_TRUE(venueStore->expectedIncomingSeq() == 3);
}

TEST(withoutTheFlagAFreshStoreTripsTheTooLowDisconnect) {
    // The control: same stale venue store, plain logon. The client's seq 1
    // is below the venue's expected 55 with no PossDup -- the session must
    // die exactly the way 141=Y exists to prevent.
    FixServerSocket server(0);
    Recorder clientEvents;
    Recorder venueEvents;

    std::future<std::unique_ptr<FixSession>> accepting = std::async(std::launch::async, [&] {
        return FixSession::accept(server, FixSession::Config("VENUE", "CLIENT", 30), venueEvents,
                                  staleStore());
    });
    std::future<std::unique_ptr<FixSession>> initiating = std::async(std::launch::async, [&] {
        return FixSession::initiate("127.0.0.1", server.port(),
                                    FixSession::Config("CLIENT", "VENUE", 30), clientEvents);
    });

    EXPECT_TRUE(venueEvents.disconnected.await(5));
    EXPECT_TRUE(venueEvents.reason().find("too low") != std::string::npos);

    try {
        accepting.get();
    } catch (...) {
        // a failed/aborted handshake is this test's point
    }
    try {
        initiating.get();
    } catch (...) {
    }
}

QTEST_MAIN()
