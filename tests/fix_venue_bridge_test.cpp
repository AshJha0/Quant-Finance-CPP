// End to end over loopback: gateway, bridge and initiator session on one
// side, a scripted acceptor on the other. Every venue answer is sent by hand
// from the test body, so each ordering is deliberate.
#include "quantfinlib/trading/fix_venue_bridge.hpp"

#include <chrono>
#include <deque>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

#include "qtest.hpp"

using namespace quantfinlib;

namespace {

bool waitFor(const std::function<bool()>& condition) {
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        if (condition()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return condition();
}

/** Records what arrives; answers nothing unless told. */
class ScriptedVenue : public FixSession::Listener {
public:
    void onNewOrderSingle(FixSession&, const NewOrderSingle& o) override {
        std::lock_guard<std::mutex> lock(m_);
        orders.push_back(o);
    }
    void onOrderCancelRequest(FixSession&, const OrderCancelRequest& r) override {
        std::lock_guard<std::mutex> lock(m_);
        cancels.push_back(r);
    }
    void onOrderCancelReplace(FixSession&, const OrderCancelReplaceRequest& r) override {
        std::lock_guard<std::mutex> lock(m_);
        replaces.push_back(r);
    }
    void onMessage(FixSession&, const FixMessage& m) override {
        if (m.msgType() == FixMessage::HEARTBEAT) {
            std::lock_guard<std::mutex> lock(m_);
            heartbeat_ = m.getString(FixMessage::TEST_REQ_ID, "");
        }
    }
    std::string lastHeartbeat() {
        std::lock_guard<std::mutex> lock(m_);
        return heartbeat_;
    }
    std::size_t orderCount() {
        std::lock_guard<std::mutex> lock(m_);
        return orders.size();
    }
    std::size_t cancelCount() {
        std::lock_guard<std::mutex> lock(m_);
        return cancels.size();
    }
    std::size_t replaceCount() {
        std::lock_guard<std::mutex> lock(m_);
        return replaces.size();
    }

    std::deque<NewOrderSingle> orders;
    std::deque<OrderCancelRequest> cancels;
    std::deque<OrderCancelReplaceRequest> replaces;

private:
    std::mutex m_;
    std::string heartbeat_;
};

struct Rig {
    HftRiskGate gate{2};
    HftOrderGateway gateway{256, gate, false};
    FixVenueBridge bridge{gateway, {"AAA", "BBB"}};
    ScriptedVenue venue;
    FixServerSocket server{0};
    std::shared_ptr<FixSessionStore> clientStore = std::make_shared<InMemorySessionStore>();
    std::shared_ptr<FixSessionStore> venueStore = std::make_shared<InMemorySessionStore>();
    std::unique_ptr<FixSession> client;
    std::unique_ptr<FixSession> acceptor;
    int exec = 0;
    int marker = 0;

    Rig() {
        gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(1000);
        gate.disableReferenceCheck();
    }

    ~Rig() {
        gateway.stop();
        acceptor.reset();
        client.reset();
    }

    void connect() {
        auto pending = std::async(std::launch::async, [this] {
            return FixSession::accept(server, FixSession::Config("VENUE", "US", 30), venue,
                                      venueStore);
        });
        client = FixSession::initiate("127.0.0.1", server.port(),
                                      FixSession::Config("US", "VENUE", 30), bridge, clientStore);
        acceptor = pending.get();
    }

    void start() {
        connect();
        gateway.start();
    }

    /** Submits and waits until the venue holds the order; returns its id. */
    std::int64_t sentOrder(Side side, std::int64_t qty, double price) {
        std::size_t before = venue.orderCount();
        std::int64_t id = gateway.submit(0, side, qty, price, 1);
        EXPECT_TRUE(id > 0);
        EXPECT_TRUE(waitFor([&] { return venue.orderCount() == before + 1; }));
        return id;
    }

    ExecutionReport report(char type, std::int64_t clOrdId, Side side, double last, double cum,
                           double leaves) {
        return ExecutionReport{"V1", "E" + std::to_string(++exec), type, '0',
                               std::to_string(clOrdId), "AAA", side, last, last > 0 ? 100.0 : 0,
                               cum, leaves, 0};
    }

    void ack(std::int64_t id, Side side, double qty) {
        acceptor->sendExecutionReport(report(ExecutionReport::EXEC_TYPE_NEW, id, side, 0, 0, qty));
        syncReports();
    }

    /** The client answers in order, so its Heartbeat proves all before it was handled. */
    void syncReports() {
        std::string id = std::to_string(++marker);
        acceptor->send(FixMessage::builder(FixMessage::TEST_REQUEST)
                               .field(FixMessage::TEST_REQ_ID, id));
        EXPECT_TRUE(waitFor([&] { return venue.lastHeartbeat() == id; }));
    }

    void sendFill(std::int64_t id, Side side, double last, double cum, double leaves) {
        acceptor->sendExecutionReport(
                report(ExecutionReport::EXEC_TYPE_TRADE, id, side, last, cum, leaves));
        syncReports();
    }
};

}  // namespace

TEST(orderLifeCycleCrossesTheWire) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 100, 100.5);
    EXPECT_TRUE(r.venue.orders[0].clOrdId == std::to_string(id));
    EXPECT_TRUE(r.venue.orders[0].symbol == "AAA");
    EXPECT_TRUE(r.venue.orders[0].quantity == 100);
    r.ack(id, Side::BUY, 100);

    r.sendFill(id, Side::BUY, 30, 30, 70);
    EXPECT_TRUE(r.gate.position(0) == 30);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 70);

    std::int64_t rep = r.gateway.replace(id, 40, 100.25, 2);
    EXPECT_TRUE(rep > 0);
    EXPECT_TRUE(waitFor([&] { return r.venue.replaceCount() == 1; }));
    EXPECT_TRUE(r.venue.replaces[0].origClOrdId == std::to_string(id));
    EXPECT_TRUE(r.venue.replaces[0].quantity == 70);   // FIX total: 30 filled + 40
    r.acceptor->sendExecutionReport(ExecutionReport::replaced(r.venue.replaces[0], "V1", "R1", 30));
    r.syncReports();
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 40);

    std::int64_t can = r.gateway.cancel(id, 3);
    EXPECT_TRUE(can > 0);
    EXPECT_TRUE(waitFor([&] { return r.venue.cancelCount() == 1; }));
    // The accepted replace renamed the order.
    EXPECT_TRUE(r.venue.cancels[0].origClOrdId == std::to_string(rep));
    r.acceptor->sendExecutionReport(ExecutionReport::canceled(r.venue.cancels[0], "V1", "C1", 30));
    r.syncReports();
    EXPECT_TRUE(r.gateway.openOrderCount() == 0);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(r.gate.position(0) == 30);
    EXPECT_TRUE(!r.gate.isKilled());
}

TEST(venueRejectReleasesTheReservation) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::SELL, 50, 99.0);
    r.acceptor->sendExecutionReport(
            r.report(ExecutionReport::EXEC_TYPE_REJECTED, id, Side::SELL, 0, 0, 0));
    r.syncReports();
    EXPECT_TRUE(r.gateway.openOrderCount() == 0);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::SELL) == 0);
    EXPECT_TRUE(!r.gate.isKilled());
}

TEST(cancelRejectKeepsTheOrderAndTheVenueNameForIt) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    std::int64_t can = r.gateway.cancel(id, 2);
    EXPECT_TRUE(waitFor([&] { return r.venue.cancelCount() == 1; }));
    r.acceptor->sendOrderCancelReject(OrderCancelReject{
            "V1", std::to_string(can), std::to_string(id), '0',
            OrderCancelReject::RESPONSE_TO_CANCEL, OrderCancelReject::REASON_TOO_LATE, "late"});
    r.syncReports();
    EXPECT_TRUE(r.gateway.leavesQuantity(id) == 10);

    // The refused ClOrdID was never the order's name at the venue.
    EXPECT_TRUE(r.gateway.cancel(id, 3) > 0);
    EXPECT_TRUE(waitFor([&] { return r.venue.cancelCount() == 2; }));
    EXPECT_TRUE(r.venue.cancels[1].origClOrdId == std::to_string(id));
    EXPECT_TRUE(!r.gate.isKilled());
}

TEST(replaceRejectRestoresTheOriginalReservation) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    std::int64_t rep = r.gateway.replace(id, 25, 100.0, 2);
    EXPECT_TRUE(waitFor([&] { return r.venue.replaceCount() == 1; }));
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 25);
    r.acceptor->sendOrderCancelReject(OrderCancelReject{
            "V1", std::to_string(rep), std::to_string(id), '0',
            OrderCancelReject::RESPONSE_TO_REPLACE, -1, ""});
    r.syncReports();
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 10);
}

TEST(venueCancelWithNoRequestEndsTheOrder) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    r.acceptor->sendExecutionReport(
            r.report(ExecutionReport::EXEC_TYPE_EXPIRED, id, Side::BUY, 0, 0, 0));
    r.syncReports();
    EXPECT_TRUE(r.gateway.openOrderCount() == 0);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 0);
}

TEST(unknownClOrdIdFailsClosed) {
    for (const char* bad : {"999999", "not-a-number", "-4", "12x"}) {
        Rig r;
        r.start();
        ExecutionReport e = r.report(ExecutionReport::EXEC_TYPE_NEW, 1, Side::BUY, 0, 0, 1);
        e.clOrdId = bad;
        r.acceptor->sendExecutionReport(e);
        r.syncReports();
        EXPECT_TRUE(r.bridge.unknownClOrdIdCount() == 1);
        EXPECT_TRUE((r.gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
    }
}

TEST(unknownFillIsStillBookedAndTripsTheKill) {
    Rig r;
    r.start();
    r.sendFill(4242, Side::BUY, 7, 7, 0);
    EXPECT_TRUE(r.gate.position(0) == 7);
    EXPECT_TRUE((r.gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
    EXPECT_TRUE(r.gateway.submit(0, Side::BUY, 1, 100.0, 1) == -HftRiskGate::REJECT_KILLED);
}

TEST(unknownCancelRejectFailsClosed) {
    Rig r;
    r.start();
    r.acceptor->sendOrderCancelReject(OrderCancelReject{
            "V1", "777", "776", '0', OrderCancelReject::RESPONSE_TO_CANCEL, 1, ""});
    r.syncReports();
    EXPECT_TRUE(r.bridge.unknownClOrdIdCount() == 1);
    EXPECT_TRUE(r.gate.isKilled());
}

TEST(overfillBooksTheExposureAndTripsTheKill) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    r.sendFill(id, Side::BUY, 15, 15, 0);
    EXPECT_TRUE(r.gate.position(0) == 15);
    EXPECT_TRUE(r.gateway.overfillCount() == 1);
    EXPECT_TRUE((r.gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
}

TEST(replayedFillIsNotBookedTwice) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    ExecutionReport fill = r.report(ExecutionReport::EXEC_TYPE_TRADE, id, Side::BUY, 4, 4, 6);
    r.acceptor->sendExecutionReport(fill);
    r.acceptor->sendExecutionReport(fill);
    r.syncReports();
    EXPECT_TRUE(r.gate.position(0) == 4);
    EXPECT_TRUE(r.bridge.duplicateFillCount() == 1);
    EXPECT_TRUE(!r.gate.isKilled());
}

TEST(cumQtyGapMeansAMissedFill) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    r.ack(id, Side::BUY, 10);
    r.sendFill(id, Side::BUY, 2, 5, 5);   // venue has 3 we never saw
    EXPECT_TRUE(r.gate.position(0) == 2);
    EXPECT_TRUE(r.bridge.cumQtyMismatchCount() == 1);
    EXPECT_TRUE(r.gate.isKilled());
}

TEST(badFillQuantitiesAndTradeBustsFailClosed) {
    {
        Rig r;
        r.start();
        std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
        r.sendFill(id, Side::BUY, 1.5, 1.5, 8.5);
        EXPECT_TRUE(r.bridge.faultCount() == 1);
        EXPECT_TRUE(r.gate.isKilled());
    }
    {
        Rig r;
        r.start();
        std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
        r.acceptor->sendExecutionReport(r.report('H', id, Side::BUY, 0, 0, 10));   // TradeCancel
        r.syncReports();
        EXPECT_TRUE(r.bridge.faultCount() == 1);
        EXPECT_TRUE(r.gate.isKilled());
    }
}

TEST(unreadableReportFailsClosed) {
    Rig r;
    r.start();
    // No ExecID, so FixSession cannot build the typed report. Without the
    // fault callback this fill would vanish behind a session Reject.
    r.acceptor->send(FixMessage::builder(FixMessage::EXECUTION_REPORT)
                             .field(FixMessage::ORDER_ID, std::string("V1"))
                             .field(FixMessage::EXEC_TYPE, 'F')
                             .field(FixMessage::ORD_STATUS, '2'));
    r.syncReports();
    EXPECT_TRUE(r.bridge.faultCount() == 1);
    EXPECT_TRUE((r.gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
}

TEST(smuggledSecondLastQtyFailsClosed) {
    Rig r;
    r.start();
    std::int64_t id = r.sentOrder(Side::BUY, 10, 100.0);
    FixMessage::Builder b = r.report(ExecutionReport::EXEC_TYPE_TRADE, id, Side::BUY, 1, 1, 9)
                                    .toBuilder();
    b.field(FixMessage::LAST_QTY, std::string("9"));
    r.acceptor->send(b);
    r.syncReports();
    EXPECT_TRUE(r.gate.position(0) == 0);
    EXPECT_TRUE(r.bridge.faultCount() == 1);
    EXPECT_TRUE(r.gate.isKilled());
}

TEST(sessionAndBusinessRejectsFailClosed) {
    {
        Rig r;
        r.start();
        r.acceptor->sendReject(2, "bad");
        r.syncReports();
        EXPECT_TRUE(r.gate.isKilled());
    }
    {
        Rig r;
        r.start();
        r.acceptor->send(FixMessage::builder(FixMessage::BUSINESS_MESSAGE_REJECT));
        r.syncReports();
        EXPECT_TRUE(r.gate.isKilled());
    }
}

TEST(noSessionMeansNeverSentAndRefusedBack) {
    Rig r;
    r.gateway.start();   // not connected
    std::int64_t id = r.gateway.submit(0, Side::BUY, 10, 100.0, 1);
    EXPECT_TRUE(id > 0);
    EXPECT_TRUE(waitFor([&] { return r.gateway.openOrderCount() == 0; }));
    EXPECT_TRUE(r.bridge.neverSentCount() == 1);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 0);
}

TEST(disconnectMidOrderKillsHoldsReservationsAndCancelsOnReconnect) {
    Rig r;
    r.start();
    std::int64_t working = r.sentOrder(Side::BUY, 40, 100.0);
    r.ack(working, Side::BUY, 40);
    r.sentOrder(Side::SELL, 30, 101.0);   // no ack: mid-order

    r.acceptor->close();
    EXPECT_TRUE(waitFor([&] { return !r.bridge.connected(); }));
    EXPECT_TRUE((r.gate.killMask() & HftRiskGate::KILL_VENUE_LINK) != 0);
    EXPECT_TRUE(waitFor([&] { return r.bridge.unsentCancelCount() == 2; }));

    // The venue may still hold both orders, so nothing is given back.
    EXPECT_TRUE(r.gateway.openOrderCount() == 2);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 40);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::SELL) == 30);
    EXPECT_TRUE(r.gateway.submit(1, Side::BUY, 1, 100.0, 2) == -HftRiskGate::REJECT_KILLED);

    r.acceptor.reset();
    r.client.reset();
    r.connect();
    EXPECT_TRUE(waitFor([&] { return r.venue.cancelCount() == 2; }));
    EXPECT_TRUE(r.bridge.unsentCancelCount() == 0);

    // A fill from the outage, then both cancels confirmed.
    r.sendFill(working, Side::BUY, 5, 5, 35);
    for (const OrderCancelRequest& c : r.venue.cancels) {
        r.acceptor->sendExecutionReport(ExecutionReport::canceled(c, "V1", "X" + c.clOrdId, 0));
    }
    r.syncReports();
    EXPECT_TRUE(r.gateway.openOrderCount() == 0);
    EXPECT_TRUE(r.gate.position(0) == 5);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::SELL) == 0);

    // A reconnect is not a reset: the kill waits for an operator.
    EXPECT_TRUE(r.gateway.submit(1, Side::BUY, 1, 100.0, 3) == -HftRiskGate::REJECT_KILLED);
    r.gate.reset(HftRiskGate::KILL_VENUE_LINK);
    EXPECT_TRUE(r.gateway.submit(1, Side::BUY, 1, 100.0, 4) > 0);
}

TEST(queuedOrdersStopAtTheDisconnect) {
    Rig r;
    r.connect();   // venue thread not running: everything stays queued
    r.gateway.submit(0, Side::BUY, 10, 100.0, 1);
    r.acceptor->close();
    EXPECT_TRUE(waitFor([&] { return !r.bridge.connected(); }));
    EXPECT_TRUE(waitFor([&] { return r.gateway.openOrderCount() == 0; }));
    r.gateway.pollOnce();
    EXPECT_TRUE(r.venue.orderCount() == 0);
    EXPECT_TRUE(r.gateway.blockedByKillCount() == 1);
    EXPECT_TRUE(r.gate.openQuantity(0, Side::BUY) == 0);
}

QTEST_MAIN()
