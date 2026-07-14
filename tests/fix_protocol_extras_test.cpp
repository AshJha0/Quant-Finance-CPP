// Port of FixProtocolExtrasTest.java: Username/Password logon, cancel/replace
// (35=F/G), and session Reject (35=3) -- including the auto-reject path
// when an inbound application message fails to parse.
#include "quantfinlib/fix/fix_session.hpp"

#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include "fix_test_helpers.hpp"
#include "qtest.hpp"
#include "quantfinlib/fix/execution_report.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/fix/order_cancel_replace_request.hpp"
#include "quantfinlib/fix/order_cancel_request.hpp"
#include "quantfinlib/orderbook/side.hpp"

using fix_test::CountdownFlag;
using fix_test::SyncList;
using quantfinlib::ExecutionReport;
using quantfinlib::FixMessage;
using quantfinlib::FixServerSocket;
using quantfinlib::FixSession;
using quantfinlib::NewOrderSingle;
using quantfinlib::OrderCancelReplaceRequest;
using quantfinlib::OrderCancelRequest;
using quantfinlib::Side;

namespace {

class Recorder : public FixSession::Listener {
public:
    SyncList<NewOrderSingle> orders;
    SyncList<OrderCancelRequest> cancels;
    SyncList<OrderCancelReplaceRequest> replaces;
    SyncList<ExecutionReport> reports;
    CountdownFlag rejected;
    std::atomic<std::int64_t> rejectRefSeq{-1};

    void onNewOrderSingle(FixSession&, const NewOrderSingle& order) override {
        orders.add(order);
    }

    void onOrderCancelRequest(FixSession&, const OrderCancelRequest& request) override {
        cancels.add(request);
    }

    void onOrderCancelReplace(FixSession&, const OrderCancelReplaceRequest& request) override {
        replaces.add(request);
    }

    void onExecutionReport(FixSession&, const ExecutionReport& report) override {
        reports.add(report);
    }

    void onMessage(FixSession&, const FixMessage& message) override {
        if (FixMessage::LOGON == message.msgType()) {
            std::lock_guard<std::mutex> lock(logonMutex_);
            logonMessage_ = message;
        }
    }

    void onReject(FixSession&, std::int64_t refSeqNum, const std::string& text) override {
        rejectRefSeq.store(refSeqNum);
        {
            std::lock_guard<std::mutex> lock(rejectMutex_);
            rejectText_ = text;
        }
        rejected.countDown();
    }

    bool hasLogonMessage() const {
        std::lock_guard<std::mutex> lock(logonMutex_);
        return logonMessage_.has_value();
    }

    FixMessage logonMessage() const {
        std::lock_guard<std::mutex> lock(logonMutex_);
        return *logonMessage_;
    }

    std::string rejectText() const {
        std::lock_guard<std::mutex> lock(rejectMutex_);
        return rejectText_;
    }

private:
    mutable std::mutex logonMutex_;
    std::optional<FixMessage> logonMessage_;
    mutable std::mutex rejectMutex_;
    std::string rejectText_;
};

struct SessionFixture {
    FixServerSocket server{0};
    Recorder clientEvents;
    Recorder venueEvents;
    std::unique_ptr<FixSession> client;
    std::unique_ptr<FixSession> venue;

    explicit SessionFixture(const FixSession::Config& clientConfig) {
        std::future<std::unique_ptr<FixSession>> accepting = std::async(std::launch::async, [&] {
            return FixSession::accept(server, FixSession::Config("VENUE", "CLIENT", 30), venueEvents);
        });
        client = FixSession::initiate("127.0.0.1", server.port(), clientConfig, clientEvents);
        venue = accepting.get();
    }
};

}  // namespace

TEST(credentialsTravelOnTheLogon) {
    SessionFixture f(FixSession::Config("CLIENT", "VENUE", 30).withCredentials("trader7", "s3cret"));
    AWAIT_TRUE(f.venueEvents.hasLogonMessage(), "venue missed logon");
    FixMessage logon = f.venueEvents.logonMessage();
    EXPECT_TRUE(logon.getString(FixMessage::USERNAME) == "trader7");
    EXPECT_TRUE(logon.getString(FixMessage::PASSWORD) == "s3cret");
    EXPECT_TRUE(f.client->isEstablished());
}

TEST(cancelRequestRoundTripsWithCanceledReport) {
    SessionFixture f(FixSession::Config("CLIENT", "VENUE", 30));
    f.client->sendNewOrderSingle("ord-1", "EURUSD", Side::BUY, 1'000, 1.0851, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(f.venueEvents.orders.size() == 1, "order lost");

    f.client->sendOrderCancelRequest("cxl-1", "ord-1", "EURUSD", Side::BUY, 1'000);
    AWAIT_TRUE(f.venueEvents.cancels.size() == 1, "cancel lost");
    OrderCancelRequest cancel = f.venueEvents.cancels.first();
    EXPECT_TRUE(cancel.clOrdId == "cxl-1");
    EXPECT_TRUE(cancel.origClOrdId == "ord-1");
    EXPECT_TRUE(cancel.quantity == 1'000);

    f.venue->sendExecutionReport(ExecutionReport::canceled(cancel, "X-1", "E-2", 0));
    AWAIT_TRUE(f.clientEvents.reports.size() >= 1, "canceled report lost");
    ExecutionReport report = f.clientEvents.reports.first();
    EXPECT_TRUE(report.execType == ExecutionReport::EXEC_TYPE_CANCELED);
    EXPECT_TRUE(report.ordStatus == ExecutionReport::ORD_STATUS_CANCELED);
    EXPECT_TRUE(report.clOrdId == "cxl-1");
}

TEST(cancelReplaceRoundTripsWithReplacedReport) {
    SessionFixture f(FixSession::Config("CLIENT", "VENUE", 30));
    f.client->sendNewOrderSingle("ord-1", "EURUSD", Side::BUY, 1'000, 1.0851, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(f.venueEvents.orders.size() == 1, "order lost");

    f.client->sendOrderCancelReplace("rpl-1", "ord-1", "EURUSD", Side::BUY, 2'000, 1.0845,
                                     NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(f.venueEvents.replaces.size() == 1, "replace lost");
    OrderCancelReplaceRequest replace = f.venueEvents.replaces.first();
    EXPECT_TRUE(replace.origClOrdId == "ord-1");
    EXPECT_TRUE(replace.quantity == 2'000);
    EXPECT_NEAR(replace.price, 1.0845, 0.0);

    f.venue->sendExecutionReport(ExecutionReport::replaced(replace, "X-1", "E-3", 0));
    AWAIT_TRUE(f.clientEvents.reports.size() >= 1, "replaced report lost");
    ExecutionReport report = f.clientEvents.reports.first();
    EXPECT_TRUE(report.execType == ExecutionReport::EXEC_TYPE_REPLACED);
    EXPECT_NEAR(report.leavesQty, 2'000, 0.0);
}

TEST(explicitRejectReachesThePeer) {
    SessionFixture f(FixSession::Config("CLIENT", "VENUE", 30));
    f.venue->sendReject(5, "unsupported instrument");
    EXPECT_TRUE(f.clientEvents.rejected.await(5));
    EXPECT_TRUE(f.clientEvents.rejectRefSeq.load() == 5);
    EXPECT_TRUE(f.clientEvents.rejectText() == "unsupported instrument");
    EXPECT_TRUE(f.client->isEstablished());  // session survives a reject
}

TEST(malformedApplicationMessageIsAutoRejectedNotFatal) {
    SessionFixture f(FixSession::Config("CLIENT", "VENUE", 30));
    // An ExecutionReport missing required tags: dispatch fails on the
    // client, which must answer with a session Reject instead of
    // disconnecting.
    f.venue->send(FixMessage::builder(FixMessage::EXECUTION_REPORT)
                          .field(FixMessage::ORDER_ID, std::string("X-1")));
    EXPECT_TRUE(f.venueEvents.rejected.await(5));
    EXPECT_TRUE(f.venueEvents.rejectRefSeq.load() > 0);
    EXPECT_TRUE(f.client->isEstablished() && f.venue->isEstablished());

    // Session keeps working after the reject.
    f.client->sendNewOrderSingle("ord-2", "GBPUSD", Side::SELL, 10, 1.27, NewOrderSingle::TIF_DAY);
    AWAIT_TRUE(f.venueEvents.orders.size() == 1, "session broken after reject");
}

QTEST_MAIN()
