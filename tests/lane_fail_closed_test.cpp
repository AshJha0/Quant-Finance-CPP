// Regression tests for the 2026-11-06 review, findings C1-C5 and the gate
// part of H2: position plus open against the cap, the order state machine,
// the quoter and hedger in-flight rules, the kill design, the aggregator's
// handling of unpriced positions, the paper gateway's gate call, and a
// randomised invariant over all of it.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/trading/auto_hedger.hpp"
#include "quantfinlib/trading/global_risk_aggregator.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_quoter.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/paper_trading_gateway.hpp"
#include "quantfinlib/trading/sharded_trading_engine.hpp"
#include "qtest.hpp"

using quantfinlib::AutoHedger;
using quantfinlib::GlobalRiskAggregator;
using quantfinlib::HftOrderGateway;
using quantfinlib::HftQuoter;
using quantfinlib::HftRiskGate;
using quantfinlib::OrderStatus;
using quantfinlib::PaperTradingGateway;
using quantfinlib::ShardedTradingEngine;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;
using quantfinlib::VenueMessage;

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();
constexpr double Inf = std::numeric_limits<double>::infinity();

void configure(HftRiskGate& gate, std::int64_t cap) {
    gate.maxOrderQuantity(1'000'000).maxOrderNotional(1e12).maxPositionQuantity(cap)
        .priceCollarPct(0.5);
    for (int s = 0; s < gate.symbolCapacity(); s++) {
        gate.setReferencePrice(s, 100.0);
    }
}

// Drives a gateway with no venue thread and records what reached the wire.
struct Wire {
    std::vector<VenueMessage> sent;
    explicit Wire(HftOrderGateway& gw) {
        gw.addVenueListener([this](const VenueMessage& m) { sent.push_back(m); });
    }
    int count(VenueMessage::Type type) const {
        int n = 0;
        for (const auto& m : sent) {
            n += m.type == type;
        }
        return n;
    }
};

std::int64_t liveOrder(HftOrderGateway& gw, Side side, std::int64_t qty, double price) {
    std::int64_t id = gw.submit(0, side, qty, price, 1);
    gw.pollOnce();
    gw.onOrderAck(id);
    return id;
}

template <typename Pred>
bool waitFor(Pred pred, int maxMs = 3000) {
    for (int i = 0; i < maxMs; i++) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

}  // namespace

// ---- C4 and H2: bad input ------------------------------------------------

TEST(freshGateRejectsEverything) {
    HftRiskGate gate(1);
    gate.setReferencePrice(0, 100.0);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) != HftRiskGate::OK);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1'000'000'000'000'000LL, 1e15) != HftRiskGate::OK);
    gate.maxOrderQuantity(10).maxOrderNotional(1e6).maxPositionQuantity(10);
    // Limits alone are not enough: the collar is still unconfigured.
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_PRICE_COLLAR);
    gate.priceCollarPct(0.1);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::OK);
}

TEST(badPricesRejectBeforeAnyOtherCheck) {
    HftRiskGate gate(1);
    configure(gate, 100);
    for (double p : {NaN, Inf, -Inf, 0.0, -1.0}) {
        EXPECT_TRUE(gate.check(0, Side::BUY, 1, p) == HftRiskGate::REJECT_PRICE);
        EXPECT_TRUE(gate.check(0, Side::SELL, 1, p) == HftRiskGate::REJECT_PRICE);
    }
    gate.kill(true);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, NaN) == HftRiskGate::REJECT_PRICE);
}

TEST(missingReferenceRejectsUnlessSwitchedOffExplicitly) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10).maxOrderNotional(1e6).maxPositionQuantity(10).priceCollarPct(0.1);
    EXPECT_TRUE(gate.check(1, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_PRICE_COLLAR);
    gate.disableReferenceCheck();
    EXPECT_TRUE(gate.check(1, Side::BUY, 1, 100.0) == HftRiskGate::OK);
}

TEST(badReferenceUpdateKeepsTheLastGoodOne) {
    HftRiskGate gate(1);
    configure(gate, 100);
    gate.setReferencePrice(0, NaN);
    gate.setReferencePrice(0, -5.0);
    EXPECT_TRUE(gate.referencePrice(0) == 100.0);
    EXPECT_TRUE(gate.badReferenceCount() == 2);
    gate.clearReferencePrice(0);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_PRICE_COLLAR);
}

TEST(badSymbolIdsAreRejectedNotDereferenced) {
    HftRiskGate gate(2);
    configure(gate, 100);
    for (int id : {-1, 2, 1 << 30}) {
        EXPECT_TRUE(gate.check(id, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_SYMBOL);
        EXPECT_TRUE(!gate.onFill(id, Side::BUY, 1));
        EXPECT_TRUE(gate.position(id) == 0);
        gate.halt(id, true);
        gate.setReferencePrice(id, 1.0);
        EXPECT_TRUE(std::isnan(gate.referencePrice(id)));
    }
    HftOrderGateway gw(gate);
    EXPECT_TRUE(gw.submit(9, Side::BUY, 1, 100.0, 1) == -HftRiskGate::REJECT_SYMBOL);
}

TEST(badConfigurationThrows) {
    HftRiskGate gate(1);
    EXPECT_THROWS(HftRiskGate(0));
    EXPECT_THROWS(gate.maxOrderQuantity(-1));
    EXPECT_THROWS(gate.maxOrderNotional(NaN));
    EXPECT_THROWS(gate.maxPositionQuantity(-1));
    EXPECT_THROWS(gate.priceCollarPct(NaN));
    EXPECT_THROWS(gate.priceCollarPct(0.0));
    EXPECT_THROWS(gate.priceCollarPct(Inf));
}

TEST(nanTickMakesNoOrderAndPullsQuotes) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(1024, gate, false);
    Wire wire(gw);
    HftQuoter quoter(gw, 1, HftQuoter::Config::of(10, 0.05));
    quoter.onTick(0, 100.0, 1, 1);
    gw.pollOnce();
    quoter.onTick(0, NaN, 1, 2);
    quoter.onTick(0, -1.0, 1, 3);
    gw.pollOnce();
    EXPECT_TRUE(quoter.badTicks() == 2);
    EXPECT_TRUE(wire.count(VenueMessage::Type::NEW) == 2);
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 2);
    for (const auto& m : wire.sent) {
        EXPECT_TRUE(std::isfinite(m.price));
    }
}

// ---- C1: reservations ----------------------------------------------------

TEST(openOrdersCountAgainstTheCap) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    EXPECT_TRUE(gate.reserve(0, Side::BUY, 1000, 100.0) == HftRiskGate::OK);
    EXPECT_TRUE(gate.reserve(0, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_POSITION);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_POSITION);
    // The sell side has its own worst case.
    EXPECT_TRUE(gate.reserve(0, Side::SELL, 1000, 100.0) == HftRiskGate::OK);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 1000);
    gate.releaseOpen(0, Side::BUY, 400);
    EXPECT_TRUE(gate.reserve(0, Side::BUY, 400, 100.0) == HftRiskGate::OK);
}

TEST(hugeCapDoesNotWrapTheWorstCase) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(std::numeric_limits<std::int64_t>::max())
        .maxOrderNotional(1e300)
        .maxPositionQuantity(std::numeric_limits<std::int64_t>::max())
        .disableReferenceCheck();
    gate.onFill(0, Side::BUY, std::numeric_limits<std::int64_t>::max() - 5);
    EXPECT_TRUE(gate.check(0, Side::BUY, 100, 1.0) == HftRiskGate::OK);   // saturates at the cap
    HftRiskGate small(1);
    small.maxOrderQuantity(std::numeric_limits<std::int64_t>::max())
        .maxOrderNotional(1e300)
        .maxPositionQuantity(1000)
        .disableReferenceCheck();
    small.onFill(0, Side::BUY, std::numeric_limits<std::int64_t>::max() - 5);
    EXPECT_TRUE(small.check(0, Side::BUY, 100, 1.0) == HftRiskGate::REJECT_POSITION);
}

TEST(gatewayHoldsTheCapAcrossUnfilledOrders) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(4096, gate, false);
    int accepted = 0;
    for (int i = 0; i < 2000; i++) {
        accepted += gw.submit(0, Side::BUY, 1000, 100.0, i) > 0;
    }
    EXPECT_TRUE(accepted == 1);
}

TEST(reservationIsReleasedOnFillCancelAndReject) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);

    std::int64_t a = liveOrder(gw, Side::BUY, 100, 100.0);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 100);
    EXPECT_TRUE(gw.onFill(a, 0, Side::BUY, 40));
    EXPECT_TRUE(gate.position(0) == 40);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 60);
    EXPECT_TRUE(gw.leavesQuantity(a) == 60);
    std::int64_t cancelId = gw.cancel(a, 2);
    EXPECT_TRUE(cancelId > 0);
    gw.pollOnce();
    EXPECT_TRUE(gw.onCancelAck(cancelId));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.openOrderCount() == 0);

    std::int64_t b = gw.submit(0, Side::BUY, 60, 100.0, 3);
    gw.pollOnce();
    EXPECT_TRUE(gw.onOrderReject(b));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gate.position(0) == 40);

    std::int64_t c = liveOrder(gw, Side::BUY, 60, 100.0);
    EXPECT_TRUE(gw.onFill(c, 0, Side::BUY, 60));
    EXPECT_TRUE(gate.position(0) == 100);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.openOrderCount() == 0);
}

TEST(ringFullGivesTheReservationBack) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(4, gate, false);
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 100.0, i) > 0);
    }
    EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 100.0, 9) == 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 4);
}

TEST(fullOrderTableFailsClosedAndLongLivedOrdersAreNotOverwritten) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(16, gate, false, 4);
    Wire wire(gw);
    std::int64_t first = liveOrder(gw, Side::BUY, 1, 100.0);
    std::int64_t others[3];
    for (auto& id : others) {
        id = liveOrder(gw, Side::BUY, 1, 100.0);
    }
    EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 100.0, 9) == -HftRiskGate::REJECT_ORDER_TABLE);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 4);
    // Free one slot: the next id must skip the slots still in use.
    EXPECT_TRUE(gw.onFill(others[1], 0, Side::BUY, 1));
    std::int64_t next = gw.submit(0, Side::BUY, 1, 100.0, 9);
    EXPECT_TRUE(next > 0);
    EXPECT_TRUE(gw.leavesQuantity(first) == 1);
    EXPECT_TRUE(gw.leavesQuantity(next) == 1);
}

// ---- C2: state machine and quoter ----------------------------------------

TEST(cancelOfAQueuedOrderNeverReachesTheVenue) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = gw.submit(0, Side::BUY, 10, 100.0, 1);
    EXPECT_TRUE(gw.cancel(id, 2) > 0);
    gw.pollOnce();
    EXPECT_TRUE(wire.sent.empty());
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.cancel(id, 3) == -HftRiskGate::REJECT_ORDER_STATE);
}

TEST(replaceCarriesClOrdIdChainAndFixTotal) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 100, 100.0);
    EXPECT_TRUE(gw.onFill(id, 0, Side::BUY, 30));

    std::int64_t r = gw.replace(id, 50, 101.0, 2);
    EXPECT_TRUE(r > 0 && r != id);
    gw.pollOnce();
    const VenueMessage& m = wire.sent.back();
    EXPECT_TRUE(m.type == VenueMessage::Type::REPLACE);
    EXPECT_TRUE(m.clOrdId == r);
    EXPECT_TRUE(m.origClOrdId == id);
    EXPECT_TRUE(m.quantity == 80);   // 30 filled + 50 leaves
    // Still 70 reserved: the old order can fill until the venue answers.
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 70);
    // One request at a time.
    EXPECT_TRUE(gw.replace(id, 10, 100.0, 3) == -HftRiskGate::REJECT_ORDER_STATE);
    EXPECT_TRUE(gw.onReplaceAck(r));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 50);
    EXPECT_TRUE(gw.leavesQuantity(id) == 50);
    // Reports may quote either ClOrdID.
    EXPECT_TRUE(gw.onFill(id, 0, Side::BUY, 10));
    EXPECT_TRUE(gw.onFill(r, 0, Side::BUY, 10));
    EXPECT_TRUE(gw.leavesQuantity(id) == 30);
}

TEST(upsizingReplaceReservesTheDifferenceAndRejectRestoresIt) {
    HftRiskGate gate(1);
    configure(gate, 150);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 100, 100.0);
    EXPECT_TRUE(gw.replace(id, 151, 100.0, 2) == -HftRiskGate::REJECT_POSITION);
    std::int64_t r = gw.replace(id, 150, 100.0, 2);
    EXPECT_TRUE(r > 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 150);
    gw.pollOnce();
    EXPECT_TRUE(gw.onReplaceReject(r));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 100);
    EXPECT_TRUE(gw.leavesQuantity(id) == 100);
}

TEST(sameSizeReplaceFitsAtTheCap) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 100, 100.0);
    EXPECT_TRUE(gw.replace(id, 100, 101.0, 2) > 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 100);
}

TEST(replaceFacesTheGateChecks) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    EXPECT_TRUE(gw.replace(id, 10, NaN, 2) == -HftRiskGate::REJECT_PRICE);
    EXPECT_TRUE(gw.replace(id, 10, 1000.0, 2) == -HftRiskGate::REJECT_PRICE_COLLAR);
    EXPECT_TRUE(gw.replace(id, 0, 100.0, 2) == -HftRiskGate::REJECT_QUANTITY);
    EXPECT_TRUE(gw.replace(12345, 1, 100.0, 2) == -HftRiskGate::REJECT_ORDER_STATE);
}

TEST(unackedOrderCannotBeReplaced) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = gw.submit(0, Side::BUY, 10, 100.0, 1);
    EXPECT_TRUE(gw.replace(id, 10, 101.0, 2) == -HftRiskGate::REJECT_ORDER_STATE);
    gw.pollOnce();
    EXPECT_TRUE(gw.replace(id, 10, 101.0, 2) == -HftRiskGate::REJECT_ORDER_STATE);
}

TEST(cancelRejectLeavesTheOrderWorking) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    std::int64_t c = gw.cancel(id, 2);
    EXPECT_TRUE(gw.cancel(id, 2) == -HftRiskGate::REJECT_ORDER_STATE);
    EXPECT_TRUE(gw.onCancelReject(c));
    EXPECT_TRUE(gw.leavesQuantity(id) == 10);
    EXPECT_TRUE(gw.replace(id, 10, 101.0, 3) > 0);
}

TEST(overfillBooksThePositionAndLatches) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    EXPECT_TRUE(!gw.onFill(id, 0, Side::BUY, 15));
    EXPECT_TRUE(gate.position(0) == 15);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.overfillCount() == 1);
    EXPECT_TRUE((gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
}

TEST(unsolicitedFillsBookThePositionAndLatch) {
    HftRiskGate gate(2);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    // Unknown id.
    EXPECT_TRUE(!gw.onFill(999, 0, Side::SELL, 7));
    EXPECT_TRUE(gate.position(0) == -7);
    EXPECT_TRUE(gw.unsolicitedFillCount() == 1);
    EXPECT_TRUE(gate.isKilled());
    gate.reset(HftRiskGate::KILL_GATEWAY);
    gw.pollOnce();   // the sweep for that trip, before the next order

    // Late fill on a finished order.
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    EXPECT_TRUE(gw.onFill(id, 0, Side::BUY, 10));
    EXPECT_TRUE(!gw.onFill(id, 0, Side::BUY, 1));
    EXPECT_TRUE(gw.unsolicitedFillCount() == 2);
    gate.reset(HftRiskGate::KILL_GATEWAY);
    gw.pollOnce();   // the sweep for that trip, before the next order

    // Wrong side and wrong symbol.
    std::int64_t other = liveOrder(gw, Side::BUY, 10, 100.0);
    EXPECT_TRUE(!gw.onFill(other, 0, Side::SELL, 1));
    EXPECT_TRUE(!gw.onFill(other, 1, Side::BUY, 1));
    EXPECT_TRUE(gw.unsolicitedFillCount() == 4);
    EXPECT_TRUE(gate.position(1) == 1);

    // Malformed quantity books nothing and still latches.
    gate.reset(HftRiskGate::KILL_GATEWAY);
    gw.pollOnce();   // the sweep for that trip, before the next order
    std::int64_t before = gate.position(0);
    EXPECT_TRUE(!gw.onFill(other, 0, Side::BUY, 0));
    EXPECT_TRUE(!gw.onFill(other, 0, Side::BUY, -5));
    EXPECT_TRUE(gate.position(0) == before);
    EXPECT_TRUE(gate.isKilled());
}

TEST(staleReportsAreCountedNotApplied) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    EXPECT_TRUE(!gw.onOrderAck(77));
    EXPECT_TRUE(!gw.onCancelAck(77));
    EXPECT_TRUE(!gw.onReplaceAck(77));
    EXPECT_TRUE(gw.unmatchedReportCount() == 3);
    EXPECT_TRUE(!gate.isKilled());
}

TEST(quoterAmendsInsteadOfStacking) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(100).priceCollarPct(0.02);
    gate.setReferencePrice(0, 100.0);
    HftOrderGateway gw(4096, gate, false);
    Wire wire(gw);
    HftQuoter quoter(gw, 1, HftQuoter::Config(100, 0.05, 0.0, 0, 0.0, nullptr));
    int acks = 0;
    std::size_t seen = 0;
    for (int t = 0; t <= 1000; t++) {
        quoter.onTick(0, 100.0 + (t % 7) * 0.01, 1, t);
        gw.pollOnce();
        for (; seen < wire.sent.size(); seen++) {
            const VenueMessage& m = wire.sent[seen];
            acks += m.type == VenueMessage::Type::NEW ? gw.onOrderAck(m.clOrdId)
                                                      : gw.onReplaceAck(m.clOrdId);
        }
    }
    EXPECT_TRUE(wire.count(VenueMessage::Type::NEW) == 2);
    EXPECT_TRUE(wire.count(VenueMessage::Type::REPLACE) > 500);
    EXPECT_TRUE(gw.openOrderCount() == 2);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 100);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 100);
    EXPECT_TRUE(quoter.rejectedSides() == 0);
    EXPECT_TRUE(acks == static_cast<int>(wire.sent.size()));
}

TEST(quoterWithoutAcksHoldsTwoOrdersAndWaits) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(4096, gate, false);
    HftQuoter quoter(gw, 1, HftQuoter::Config::of(100, 0.05));
    for (int t = 0; t <= 1000; t++) {
        quoter.onTick(0, 100.0 + t * 0.001, 1, t);
    }
    EXPECT_TRUE(gw.submittedCount() == 2);
    EXPECT_TRUE(quoter.pendingSides() > 0);
    EXPECT_TRUE(quoter.rejectedSides() == 0);
}

TEST(quoterResubmitsAFilledSide) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    HftQuoter quoter(gw, 1, HftQuoter::Config::of(10, 0.05));
    quoter.onTick(0, 100.0, 1, 1);
    gw.pollOnce();
    std::int64_t bid = quoter.workingOrder(0, Side::BUY);
    gw.onOrderAck(bid);
    gw.onOrderAck(quoter.workingOrder(0, Side::SELL));
    EXPECT_TRUE(gw.onFill(bid, 0, Side::BUY, 10));
    quoter.onTick(0, 100.0, 1, 2);
    EXPECT_TRUE(quoter.workingOrder(0, Side::BUY) != bid);
    EXPECT_TRUE(gw.openOrderCount() == 2);
}

TEST(quoterRisingFastDoesNotSelfCross) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(1000)
        .disableReferenceCheck();
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    HftQuoter quoter(gw, 1, HftQuoter::Config::of(10, 0.05));
    quoter.onTick(0, 100.0, 1, 1);
    gw.pollOnce();
    gw.onOrderAck(quoter.workingOrder(0, Side::BUY));
    gw.onOrderAck(quoter.workingOrder(0, Side::SELL));
    // The old ask is 100.05. Its pending replace still counts until acked, so
    // the bid at 109.95 waits, but the ask must have gone first.
    quoter.onTick(0, 110.0, 1, 2);
    gw.pollOnce();
    EXPECT_TRUE(wire.sent.back().type == VenueMessage::Type::REPLACE);
    EXPECT_TRUE(wire.sent.back().side == Side::SELL);
}

// ---- self-trade prevention -----------------------------------------------

TEST(crossingOwnOrderIsRejectedAndNonCrossingPasses) {
    HftRiskGate gate(2);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    liveOrder(gw, Side::SELL, 10, 100.0);
    EXPECT_TRUE(gw.submit(0, Side::BUY, 10, 100.0, 2) == -HftRiskGate::REJECT_SELF_TRADE);
    EXPECT_TRUE(gw.submit(0, Side::BUY, 10, 101.0, 2) == -HftRiskGate::REJECT_SELF_TRADE);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gate.rejectionCount(HftRiskGate::REJECT_SELF_TRADE) == 2);
    EXPECT_TRUE(gw.submit(0, Side::BUY, 10, 99.99, 2) > 0);
    EXPECT_TRUE(gw.submit(1, Side::BUY, 10, 101.0, 2) > 0);   // other symbol
}

TEST(replaceThatCrossesOwnOrderIsRejected) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    liveOrder(gw, Side::SELL, 10, 100.0);
    std::int64_t bid = liveOrder(gw, Side::BUY, 10, 99.0);
    EXPECT_TRUE(gw.replace(bid, 20, 100.0, 2) == -HftRiskGate::REJECT_SELF_TRADE);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 10);
}

TEST(selfTradeHookCanAllowACross) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    int asked = 0;
    gw.setSelfTradeHook(
            [](void* ctx, const HftOrderGateway::SelfTradeCandidate& c) {
                ++*static_cast<int*>(ctx);
                return c.restingPrice == 100.0;
            },
            &asked);
    liveOrder(gw, Side::SELL, 10, 100.0);
    EXPECT_TRUE(gw.submit(0, Side::BUY, 10, 100.0, 2) > 0);
    EXPECT_TRUE(asked == 1);
}

// ---- C3: wiring ----------------------------------------------------------

TEST(engineFeedsTheGateReferenceAndGatewayFeedsItsPosition) {
    ShardedTradingEngine engine(1, 64, 64, 4, false, [](int) {
        auto gate = std::make_unique<HftRiskGate>(4);
        gate->maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(100)
            .priceCollarPct(0.02);
        return gate;
    });
    int handle = engine.registerSymbol("X", {0});
    int id = engine.localId(handle, 0);
    EXPECT_TRUE(engine.gate(0).check(id, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_PRICE_COLLAR);
    engine.publish(handle, 100.0, 1, 1);
    EXPECT_TRUE(engine.gate(0).referencePrice(id) == 100.0);
    EXPECT_TRUE(engine.gate(0).check(id, Side::BUY, 1, 100.0) == HftRiskGate::OK);
    engine.publish(handle, NaN, 1, 2);
    EXPECT_TRUE(engine.gate(0).referencePrice(id) == 100.0);

    std::int64_t order = engine.gateway(0).submit(id, Side::BUY, 10, 100.0, 3);
    engine.gateway(0).pollOnce();
    engine.gateway(0).onOrderAck(order);
    engine.gateway(0).onFill(order, id, Side::BUY, 10);
    EXPECT_TRUE(engine.gate(0).position(id) == 10);
}

// ---- hedger --------------------------------------------------------------

TEST(hedgerCountsInFlightAndAmendsRatherThanStacks) {
    HftRiskGate gate(1);
    configure(gate, 10000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    gate.onFill(0, Side::BUY, 800);
    AutoHedger hedger(gw, 1, 500, 0);

    hedger.onTick(0, 100.0, 1, 1);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    for (int t = 2; t < 100; t++) {
        hedger.onTick(0, 100.0, 1, t);   // no fill yet, no cooldown: must not stack
    }
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 300);

    gw.pollOnce();
    std::int64_t id = wire.sent[0].clOrdId;
    gw.onOrderAck(id);
    gate.onFill(0, Side::BUY, 200);   // excess grows to 500
    hedger.onTick(0, 100.0, 1, 200);
    EXPECT_TRUE(hedger.hedgesAmended() == 1);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 500);
}

TEST(hedgerSendsANewHedgeOnlyAfterTheFirstIsDone) {
    HftRiskGate gate(1);
    configure(gate, 10000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    gate.onFill(0, Side::BUY, 800);
    AutoHedger hedger(gw, 1, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    gw.pollOnce();
    std::int64_t id = wire.sent[0].clOrdId;
    gw.onOrderAck(id);
    EXPECT_TRUE(gw.onFill(id, 0, Side::SELL, 300));
    EXPECT_TRUE(gate.position(0) == 500);
    hedger.onTick(0, 100.0, 1, 2);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);   // at the band: done
    gate.onFill(0, Side::BUY, 50);
    hedger.onTick(0, 100.0, 1, 3);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 2);
}

TEST(hedgerCancelsAWorkingHedgeOnceBackInsideTheBand) {
    HftRiskGate gate(1);
    configure(gate, 10000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    gate.onFill(0, Side::BUY, 800);
    AutoHedger hedger(gw, 1, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    gw.pollOnce();
    gw.onOrderAck(wire.sent[0].clOrdId);
    gate.onFill(0, Side::SELL, 400);   // inventory left some other way
    hedger.onTick(0, 100.0, 1, 2);
    gw.pollOnce();
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 1);
}

// ---- C5: kill ------------------------------------------------------------

TEST(queuedOrdersDoNotReachTheVenueAfterKill) {
    HftRiskGate gate(1);
    configure(gate, 1'000'000);
    HftOrderGateway gw(4096, gate, false);
    std::atomic<int> delivered{0};
    gw.addOrderListener([&](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {
        delivered.fetch_add(1);
    });
    for (int i = 0; i < 1000; i++) {
        EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 100.0, i) > 0);
    }
    gate.kill(true);
    gw.start();
    gw.stop();
    EXPECT_TRUE(delivered.load() == 0);
    EXPECT_TRUE(gw.blockedByKillCount() == 1000);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.openOrderCount() == 0);
}

TEST(killSweepsWorkingOrdersWithCancels) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t a = liveOrder(gw, Side::BUY, 10, 99.0);
    std::int64_t b = liveOrder(gw, Side::SELL, 10, 101.0);
    gate.trip(HftRiskGate::KILL_AGGREGATOR);
    gw.pollOnce();
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 2);
    for (const auto& m : wire.sent) {
        if (m.type == VenueMessage::Type::CANCEL) {
            EXPECT_TRUE(gw.onCancelAck(m.clOrdId));
        }
    }
    EXPECT_TRUE(gw.leavesQuantity(a) == 0 && gw.leavesQuantity(b) == 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    // One sweep per trip, not one per poll.
    gw.pollOnce();
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 2);
}

TEST(cancelAllWorksWithoutKillAndRecoversFromAFullRing) {
    HftRiskGate gate(1);
    configure(gate, 100000);
    HftOrderGateway gw(4, gate, false);
    Wire wire(gw);
    for (int i = 0; i < 10; i++) {
        liveOrder(gw, Side::BUY, 1, 100.0);
    }
    gw.cancelAll(1);   // the ring holds 4 of 10
    for (int i = 0; i < 10; i++) {
        gw.pollOnce();
    }
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 10);
}

TEST(killAllowsOnlyStrictlyReducingOrders) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    gate.onFill(0, Side::BUY, 100);
    gate.kill(true);
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::REJECT_KILLED);
    EXPECT_TRUE(gate.check(0, Side::SELL, 101, 100.0) == HftRiskGate::REJECT_KILLED);   // flips
    EXPECT_TRUE(gate.reserve(0, Side::SELL, 60, 100.0) == HftRiskGate::OK);
    // Reducing orders already open count: 60 + 41 would flip.
    EXPECT_TRUE(gate.reserve(0, Side::SELL, 41, 100.0) == HftRiskGate::REJECT_KILLED);
    EXPECT_TRUE(gate.reserve(0, Side::SELL, 40, 100.0) == HftRiskGate::OK);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 100);
    // Reduce-only still faces the other checks.
    gate.releaseOpen(0, Side::SELL, 100);
    EXPECT_TRUE(gate.check(0, Side::SELL, 10, 1000.0) == HftRiskGate::REJECT_PRICE_COLLAR);
}

TEST(reduceOnlyOrderReachesTheVenueDuringKill) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    gate.onFill(0, Side::BUY, 100);
    gw.pollOnce();
    gate.kill(true);
    std::int64_t id = gw.submit(0, Side::SELL, 100, 100.0, 1);
    EXPECT_TRUE(id > 0);
    gw.pollOnce();   // sweep runs here and must spare it
    EXPECT_TRUE(wire.count(VenueMessage::Type::NEW) == 1);
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 0);
}

TEST(killBlocksNonReducingReplaceButNotCancel) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    gate.kill(true);
    EXPECT_TRUE(gw.replace(id, 10, 101.0, 2) == -HftRiskGate::REJECT_KILLED);
    EXPECT_TRUE(gw.cancel(id, 2) > 0);
}

TEST(queuedReplaceIsStoppedByKill) {
    HftRiskGate gate(1);
    configure(gate, 1000);
    HftOrderGateway gw(64, gate, false);
    Wire wire(gw);
    std::int64_t id = liveOrder(gw, Side::BUY, 10, 100.0);
    EXPECT_TRUE(gw.replace(id, 50, 101.0, 2) > 0);
    gate.kill(true);
    gw.pollOnce();
    EXPECT_TRUE(wire.count(VenueMessage::Type::REPLACE) == 0);
    EXPECT_TRUE(wire.count(VenueMessage::Type::CANCEL) == 1);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 10);
}

TEST(killSourcesLatchIndependently) {
    HftRiskGate gate(1);
    configure(gate, 100);
    gate.trip(HftRiskGate::KILL_AGGREGATOR);
    gate.kill(true);
    gate.reset(HftRiskGate::KILL_AGGREGATOR);
    EXPECT_TRUE(gate.isKilled());
    EXPECT_TRUE(gate.killMask() == HftRiskGate::KILL_OPERATOR);
    gate.trip(HftRiskGate::KILL_DEAD_MAN);
    gate.kill(false);
    EXPECT_TRUE(gate.isKilled());
    gate.reset(HftRiskGate::KILL_DEAD_MAN);
    EXPECT_TRUE(!gate.isKilled());
    EXPECT_TRUE(gate.check(0, Side::BUY, 1, 100.0) == HftRiskGate::OK);
}

TEST(aggregatorDoesNotClearAnOperatorKillOrResumeByItself) {
    HftRiskGate gate(1);
    configure(gate, 1'000'000);
    gate.onFill(0, Side::BUY, 1000);
    GlobalRiskAggregator agg({&gate}, 50'000.0, 0.5, 200'000);
    EXPECT_TRUE(waitFor([&] { return agg.isTripped(); }));
    gate.kill(true);   // operator kill during the trip
    gate.onFill(0, Side::SELL, 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(agg.isTripped());   // flat, yet still latched
    EXPECT_TRUE((gate.killMask() & HftRiskGate::KILL_AGGREGATOR) != 0);
    EXPECT_TRUE(agg.manualReset());
    EXPECT_TRUE(gate.killMask() == HftRiskGate::KILL_OPERATOR);
    agg.close();
}

TEST(unpricedNonZeroPositionTripsTheAggregator) {
    HftRiskGate gate(1);
    gate.onFill(0, Side::BUY, 1'000'000'000);
    GlobalRiskAggregator agg({&gate}, 1e15, 0.9, 200'000);
    EXPECT_TRUE(waitFor([&] { return agg.isTripped(); }));
    EXPECT_TRUE(agg.unpricedPositions() == 1);
    EXPECT_TRUE(gate.isKilled());
    EXPECT_TRUE(!agg.manualReset());   // still cannot be valued
    gate.setReferencePrice(0, 1.0);
    EXPECT_TRUE(agg.manualReset());
    agg.close();
}

TEST(deadManTripsOnSilenceAndHeartbeatHoldsItOff) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    gw.enableDeadMan(1000, 0);
    EXPECT_TRUE(!gw.checkDeadMan(1000));
    gw.heartbeat(900);
    EXPECT_TRUE(!gw.checkDeadMan(1900));
    EXPECT_TRUE(gw.checkDeadMan(1901));
    EXPECT_TRUE(gate.killMask() == HftRiskGate::KILL_DEAD_MAN);
    // Latched: a late heartbeat does not resume.
    gw.heartbeat(2000);
    EXPECT_TRUE(gate.isKilled());
}

TEST(deadManFiresFromTheVenueThreadAndSweeps) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    std::atomic<int> cancels{0};
    gw.addVenueListener([&](const VenueMessage& m) {
        if (m.type == VenueMessage::Type::NEW) {
            gw.onOrderAck(m.clOrdId);
        } else {
            cancels.fetch_add(1);
        }
    });
    gw.start();
    // The order goes first: with the timer armed it could lose the race and be refused.
    EXPECT_TRUE(gw.submit(0, Side::BUY, 10, 100.0, 1) > 0);
    EXPECT_TRUE(waitFor([&] { return gw.deliveredCount() == 1; }));
    gw.enableDeadMan(1'000'000);
    EXPECT_TRUE(waitFor([&] { return gate.isKilled() && cancels.load() == 1; }));
    gw.stop();
}

TEST(throwingListenerLatchesInsteadOfTerminating) {
    HftRiskGate gate(1);
    configure(gate, 100);
    HftOrderGateway gw(64, gate, false);
    gw.addOrderListener([](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {
        throw std::runtime_error("adapter down");
    });
    gw.submit(0, Side::BUY, 1, 100.0, 1);
    gw.pollOnce();
    EXPECT_TRUE(gw.listenerFailureCount() == 1);
    EXPECT_TRUE((gate.killMask() & HftRiskGate::KILL_GATEWAY) != 0);
}

// ---- paper gateway -------------------------------------------------------

TEST(paperGatewayValidatesWithoutAGate) {
    auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1000.0, 0, PaperTradingGateway::FillConfig::zeroLatency());
    gw.onQuote("X", 1, 99.0, 1000, 101.0, 1000);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 0)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, -5)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitLimit("X", Side::BUY, -5, 100.0)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitLimit("X", Side::BUY, 5, NaN)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitLimit("X", Side::BUY, 5, -1.0)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.position("X") == 0);
    EXPECT_TRUE(gw.snapshot().rejectionCount == 5);
    gw.onQuote("X", 2, NaN, 1000, 101.0, 1000);  // ignored: the last good quote stands
    gw.onQuote("X", 3, 102.0, 1000, 101.0, 1000);  // crossed
    EXPECT_TRUE(gw.snapshot().rejectionCount == 7);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 1)) == OrderStatus::FILLED);
}

TEST(paperGatewayCallsTheRealGate) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(100).maxOrderNotional(1e6).maxPositionQuantity(100).priceCollarPct(0.05);
    PaperTradingGateway gw(1e6, 0, gate, {{"X", 0}}, PaperTradingGateway::FillConfig::zeroLatency());
    std::int64_t nanos = 0;
    auto quote = [&](const char* symbol, double bid, double ask) {
        gw.onQuote(symbol, ++nanos, bid, 1000000, ask, 1000000);
    };
    quote("X", 99.0, 101.0);
    EXPECT_TRUE(gate.referencePrice(0) == 100.0);

    // Limits.
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 101)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitLimit("X", Side::BUY, 1, 50.0)) == OrderStatus::REJECTED);
    // Unmapped symbol.
    quote("Y", 99.0, 101.0);
    EXPECT_TRUE(gw.status(gw.submitMarket("Y", Side::BUY, 1)) == OrderStatus::REJECTED);

    // A resting order holds its reservation; cancel returns it.
    std::int64_t resting = gw.submitLimit("X", Side::BUY, 100, 98.0);
    EXPECT_TRUE(gw.status(resting) == OrderStatus::NEW);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 100);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 1)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.cancel(resting));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);

    // A fill moves the reservation into the position.
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 60)) == OrderStatus::FILLED);
    EXPECT_TRUE(gate.position(0) == 60);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);

    gate.kill(true);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::BUY, 1)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.status(gw.submitMarket("X", Side::SELL, 10)) == OrderStatus::FILLED);
}

// ---- randomised invariant ------------------------------------------------

namespace {

// position + open never exceeds the cap on either side, at every step.
bool capHolds(const HftRiskGate& gate, std::int64_t cap) {
    return gate.position(0) + gate.openQuantity(0, Side::BUY) <= cap
           && gate.position(0) - gate.openQuantity(0, Side::SELL) >= -cap;
}

}  // namespace

TEST(randomisedPositionPlusOpenNeverExceedsCap) {
    for (std::int64_t seed = 1; seed <= 20; seed++) {
        constexpr std::int64_t CAP = 500;
        HftRiskGate gate(1);
        gate.maxOrderQuantity(300).maxOrderNotional(1e9).maxPositionQuantity(CAP)
            .disableReferenceCheck();
        HftOrderGateway gw(64, gate, false, 64);
        gw.setSelfTradeHook([](void*, const HftOrderGateway::SelfTradeCandidate&) { return true; },
                            nullptr);
        struct Pending {
            VenueMessage m;
        };
        std::vector<VenueMessage> wire;
        gw.addVenueListener([&](const VenueMessage& m) { wire.push_back(m); });
        std::vector<std::int64_t> ids;
        struct Known {
            std::int64_t id;
            Side side;
        };
        std::vector<Known> working;
        SplittableRandom rnd(seed);

        for (int step = 0; step < 20000; step++) {
            int action = rnd.nextInt(100);
            Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
            if (action < 35) {
                std::int64_t id = gw.submit(0, side, 1 + rnd.nextInt(300), 100.0, step);
                if (id > 0) working.push_back({id, side});
            } else if (action < 45) {
                gw.pollOnce();
            } else if (!wire.empty() && action < 70) {
                // The venue answers one outstanding message, sometimes refusing.
                std::size_t k = static_cast<std::size_t>(rnd.nextInt(static_cast<int>(wire.size())));
                VenueMessage m = wire[k];
                wire.erase(wire.begin() + static_cast<std::ptrdiff_t>(k));
                bool refuse = rnd.nextInt(5) == 0;
                switch (m.type) {
                    case VenueMessage::Type::NEW:
                        refuse ? gw.onOrderReject(m.clOrdId) : gw.onOrderAck(m.clOrdId);
                        break;
                    case VenueMessage::Type::CANCEL:
                        refuse ? gw.onCancelReject(m.clOrdId) : gw.onCancelAck(m.clOrdId);
                        break;
                    case VenueMessage::Type::REPLACE:
                        refuse ? gw.onReplaceReject(m.clOrdId) : gw.onReplaceAck(m.clOrdId);
                        break;
                }
            } else if (!working.empty()) {
                std::size_t k = static_cast<std::size_t>(rnd.nextInt(static_cast<int>(working.size())));
                Known order = working[k];
                std::int64_t leaves = gw.leavesQuantity(order.id);
                if (leaves == 0) {
                    working[k] = working.back();
                    working.pop_back();
                } else if (action < 85) {
                    // A fill the venue could legally send. Only LIVE or SENT
                    // orders can fill; a queued one reports a fault, so poll first.
                    gw.pollOnce();
                    leaves = gw.leavesQuantity(order.id);
                    if (leaves > 0) {
                        std::int64_t qty = 1 + rnd.nextInt(static_cast<int>(leaves));
                        EXPECT_TRUE(gw.onFill(order.id, 0, order.side, qty));
                    }
                } else if (action < 93) {
                    gw.replace(order.id, 1 + rnd.nextInt(300), 100.0, step);
                } else {
                    gw.cancel(order.id, step);
                }
            }
            if (!capHolds(gate, CAP)) {
                EXPECT_TRUE(false);
                return;
            }
            EXPECT_TRUE(gate.openQuantity(0, Side::BUY) >= 0);
            EXPECT_TRUE(gate.openQuantity(0, Side::SELL) >= 0);
        }
        EXPECT_TRUE(!gate.isKilled());
        // Wind down: everything cancelled returns every reservation.
        gw.cancelAll(0);
        for (int i = 0; i < 8; i++) {
            gw.pollOnce();
            for (const auto& m : wire) {
                if (m.type == VenueMessage::Type::CANCEL) gw.onCancelAck(m.clOrdId);
                if (m.type == VenueMessage::Type::NEW) gw.onOrderAck(m.clOrdId);
                if (m.type == VenueMessage::Type::REPLACE) gw.onReplaceReject(m.clOrdId);
            }
            wire.clear();
            gw.cancelAll(0);
        }
        EXPECT_TRUE(gw.openOrderCount() == 0);
        EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
        EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 0);
    }
}

TEST(concurrentReserversAndFillsNeverBreachTheCap) {
    // Three threads race on one gate. The watcher reads open before position,
    // the order the gate itself uses, so a true breach is the only way to fail.
    constexpr std::int64_t CAP = 1000;
    HftRiskGate gate(1);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(CAP)
        .disableReferenceCheck();
    std::atomic<bool> stop{false};
    std::atomic<std::int64_t> breaches{0};
    auto worker = [&](std::int64_t seed) {
        SplittableRandom rnd(seed);
        for (int i = 0; i < 200000; i++) {
            Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
            std::int64_t qty = 1 + rnd.nextInt(100);
            if (gate.reserve(0, side, qty, 100.0) == HftRiskGate::OK) {
                if (rnd.nextBoolean()) {
                    gate.onOrderFill(0, side, qty, qty);
                } else {
                    gate.releaseOpen(0, side, qty);
                }
            }
        }
    };
    std::thread watcher([&] {
        while (!stop.load(std::memory_order_acquire)) {
            std::int64_t openBuy = gate.openQuantity(0, Side::BUY);
            std::int64_t openSell = gate.openQuantity(0, Side::SELL);
            std::int64_t position = gate.position(0);
            // A reservation that is about to be refused can show for a moment,
            // bounded by one maximum order per worker.
            if (position + openBuy > CAP + 200 || position - openSell < -CAP - 200) {
                breaches.fetch_add(1);
            }
        }
    });
    std::thread a(worker, 1), b(worker, 2);
    a.join();
    b.join();
    stop.store(true, std::memory_order_release);
    watcher.join();
    EXPECT_TRUE(breaches.load() == 0);
    EXPECT_TRUE(std::llabs(gate.position(0)) <= CAP);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 0);
}

TEST(venueThreadTradingThreadAndKillTogether) {
    // End to end under TSan: quoting, venue acks and fills, then a kill.
    HftRiskGate gate(1);
    configure(gate, 500);
    HftOrderGateway gw(1024, gate, false);
    std::atomic<int> cancels{0};
    std::atomic<bool> breached{false};
    gw.addVenueListener([&](const VenueMessage& m) {
        switch (m.type) {
            case VenueMessage::Type::NEW:
                gw.onOrderAck(m.clOrdId);
                if ((m.clOrdId & 3) == 0) gw.onFill(m.clOrdId, 0, m.side, 1);
                break;
            case VenueMessage::Type::REPLACE: gw.onReplaceAck(m.clOrdId); break;
            case VenueMessage::Type::CANCEL:
                cancels.fetch_add(1);
                gw.onCancelAck(m.clOrdId);
                break;
        }
        if (std::llabs(gate.position(0)) > 500) breached.store(true);
    });
    gw.start();
    HftQuoter quoter(gw, 1, HftQuoter::Config::of(50, 0.05));
    for (int t = 0; t < 50000; t++) {
        quoter.onTick(0, 100.0 + (t % 11) * 0.01, 1, t);
        if (t == 40000) gate.kill(true);
    }
    EXPECT_TRUE(waitFor([&] { return gw.openOrderCount() == 0; }));
    gw.stop();
    EXPECT_TRUE(!breached.load());
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gate.openQuantity(0, Side::SELL) == 0);
}

QTEST_MAIN()
