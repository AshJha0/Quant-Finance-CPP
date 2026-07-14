// Port of the Java PaperTradingGateway behavior pins: market orders fill
// at the touch, resting limits fill when the market crosses, average-cost
// accounting on adds vs. reductions, the position-flip re-anchor (a fill
// that flips the sign re-anchors avgCost to the fill price for the
// remainder), REJECTION_LOG_CAP capping the stored log while the count
// keeps going past it, snapshot() internal consistency, and a two-thread
// hammer where every fill lands exactly once (position/cash sums exact,
// no torn reads/writes under the single mutex).
#include "quantfinlib/trading/paper_trading_gateway.hpp"

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::OrderStatus;
using quantfinlib::PaperTradingGateway;
using quantfinlib::Side;

TEST(marketOrderFillsAtTheTouchAndUpdatesCashAndPosition) {
    PaperTradingGateway gw(10000.0);
    gw.onQuote("EURUSD", 1.1000, 1.1002);
    std::int64_t id = gw.submitMarket("EURUSD", Side::BUY, 100);
    EXPECT_TRUE(gw.status(id) == OrderStatus::FILLED);
    EXPECT_NEAR(gw.position("EURUSD"), 100.0, 1e-9);
    // Bought 100 @ 1.1002 (the ask): cash -= 110.02.
    EXPECT_NEAR(gw.cash(), 10000.0 - 100 * 1.1002, 1e-9);
}

TEST(marketOrderWithNoQuoteIsRejected) {
    PaperTradingGateway gw(1000.0);
    std::int64_t id = gw.submitMarket("NOQUOTE", Side::BUY, 10);
    EXPECT_TRUE(gw.status(id) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.rejectionLog().size() == 1);
}

TEST(limitOrderMarketableFillsImmediatelyOtherwiseRests) {
    PaperTradingGateway gw(10000.0);
    gw.onQuote("X", 100.0, 100.10);
    // Marketable buy (limit >= ask): fills now at the ask.
    std::int64_t marketable = gw.submitLimit("X", Side::BUY, 10, 100.20);
    EXPECT_TRUE(gw.status(marketable) == OrderStatus::FILLED);
    EXPECT_NEAR(gw.position("X"), 10.0, 1e-9);

    // Non-marketable buy rests until the market crosses it.
    std::int64_t resting = gw.submitLimit("X", Side::BUY, 10, 99.90);
    EXPECT_TRUE(gw.status(resting) == OrderStatus::NEW);
    gw.onQuote("X", 100.0, 100.10);   // doesn't cross yet
    EXPECT_TRUE(gw.status(resting) == OrderStatus::NEW);
    gw.onQuote("X", 99.80, 99.90);    // ask now <= 99.90 -> fills
    EXPECT_TRUE(gw.status(resting) == OrderStatus::FILLED);
    EXPECT_NEAR(gw.position("X"), 20.0, 1e-9);
}

TEST(cancelOnlyWorksWhileStillNewAndRemovesFromResting) {
    PaperTradingGateway gw(10000.0);
    std::int64_t resting = gw.submitLimit("X", Side::BUY, 10, 90.0);   // no quote -> rests
    EXPECT_TRUE(gw.cancel(resting));
    EXPECT_TRUE(gw.status(resting) == OrderStatus::CANCELED);
    // A later crossing quote must NOT fill the canceled order.
    gw.onQuote("X", 89.0, 89.5);
    EXPECT_TRUE(gw.status(resting) == OrderStatus::CANCELED);
    EXPECT_NEAR(gw.position("X"), 0.0, 1e-9);
    // Cancelling twice fails the second time.
    EXPECT_TRUE(!gw.cancel(resting));
}

TEST(statusOnUnknownOrderThrows) {
    PaperTradingGateway gw(1000.0);
    EXPECT_THROWS(gw.status(999));
}

TEST(averageCostBlendsOnAddsAndRealizesPnlOnReductions) {
    PaperTradingGateway gw(100000.0);
    gw.onQuote("X", 99.0, 101.0);
    gw.submitMarket("X", Side::BUY, 100);    // @101, avgCost=101
    gw.submitMarket("X", Side::BUY, 100);    // @101, avgCost stays 101 (same price)
    EXPECT_NEAR(gw.position("X"), 200.0, 1e-9);

    gw.onQuote("X", 105.0, 106.0);
    gw.submitMarket("X", Side::SELL, 50);    // sell 50 @105: realize (105-101)*50=200
    EXPECT_NEAR(gw.position("X"), 150.0, 1e-9);
    EXPECT_NEAR(gw.realizedPnl(), 200.0, 1e-6);
}

TEST(positionFlipReAnchorsAvgCostToTheFillPrice) {
    PaperTradingGateway gw(100000.0);
    gw.onQuote("X", 99.0, 100.0);
    gw.submitMarket("X", Side::BUY, 100);     // long 100 @100
    gw.onQuote("X", 90.0, 91.0);
    // Sell 150: 100 closes the long (realize (90-100)*100=-1000), remaining
    // 50 opens a NEW short re-anchored at the fill price (90), not blended
    // with the old long's avgCost.
    gw.submitMarket("X", Side::SELL, 150);
    EXPECT_NEAR(gw.position("X"), -50.0, 1e-9);
    EXPECT_NEAR(gw.realizedPnl(), -1000.0, 1e-6);

    // Confirm the re-anchor: closing the new 50-short at 95 (the ask, since
    // this is a BUY) should realize (95-90)*50 * sign(-50 short) = -250
    // more, i.e. total -1250, NOT computed against the old avgCost of 100.
    gw.onQuote("X", 94.0, 95.0);
    gw.submitMarket("X", Side::BUY, 50);
    EXPECT_NEAR(gw.position("X"), 0.0, 1e-9);
    EXPECT_NEAR(gw.realizedPnl(), -1250.0, 1e-6);
}

TEST(flatteningExactlyResetsAvgCostToZero) {
    PaperTradingGateway gw(100000.0);
    gw.onQuote("X", 99.0, 100.0);
    gw.submitMarket("X", Side::BUY, 100);
    gw.onQuote("X", 104.0, 105.0);
    gw.submitMarket("X", Side::SELL, 100);   // exact flatten
    EXPECT_NEAR(gw.position("X"), 0.0, 1e-9);
    EXPECT_TRUE(gw.positionsSnapshot().empty());   // zero positions are excluded
}

TEST(rejectionLogCapsStorageButRejectionCountKeepsCountingPastIt) {
    PaperTradingGateway gw(1.0);   // tiny cash, irrelevant to rejection path here
    constexpr int OVER_CAP = PaperTradingGateway::REJECTION_LOG_CAP + 250;
    for (int i = 0; i < OVER_CAP; i++) {
        gw.submitMarket("NEVERQUOTED", Side::BUY, 1);   // always NO_QUOTE rejected
    }
    auto snap = gw.snapshot();
    EXPECT_TRUE(snap.rejectionCount == OVER_CAP);
    EXPECT_TRUE(static_cast<int>(gw.rejectionLog().size()) == PaperTradingGateway::REJECTION_LOG_CAP);
}

TEST(snapshotIsInternallyConsistentWithEquityAndPositions) {
    PaperTradingGateway gw(50000.0);
    gw.onQuote("X", 99.0, 100.0);
    gw.submitMarket("X", Side::BUY, 100);
    gw.onQuote("X", 104.0, 105.0);   // mid 104.5

    auto snap = gw.snapshot();
    EXPECT_NEAR(snap.cash, gw.cash(), 1e-9);
    EXPECT_NEAR(snap.realizedPnl, gw.realizedPnl(), 1e-9);
    EXPECT_NEAR(snap.equity, snap.cash + 100 * 104.5, 1e-6);
    EXPECT_TRUE(snap.positions.at("X") == 100.0);
}

TEST(flatCommissionIsDeductedFromCashOnEveryFill) {
    PaperTradingGateway gw(10000.0, 0.01);   // 1% flat commission
    gw.onQuote("X", 100.0, 100.0);
    gw.submitMarket("X", Side::BUY, 10);   // notional 1000, fee 10
    EXPECT_NEAR(gw.cash(), 10000.0 - 1000.0 - 10.0, 1e-9);
}

TEST(twoThreadHammerLandsEveryFillExactlyOnceWithConsistentSums) {
    // Two threads submit market orders concurrently against a single
    // gateway guarded by one mutex: every fill must be reflected exactly
    // once in the position and cash, with no lost or duplicated updates.
    PaperTradingGateway gw(10'000'000.0);
    gw.onQuote("X", 100.0, 100.0);   // flat, frictionless quote: fills always @100

    constexpr int PER_THREAD = 20000;
    std::atomic<int> filled{0};
    gw.addExecutionListener([&](std::int64_t, const std::string&, Side, double, std::int64_t,
                                std::int64_t) { filled.fetch_add(1, std::memory_order_relaxed); });

    auto worker = [&](Side side) {
        for (int i = 0; i < PER_THREAD; i++) {
            gw.submitMarket("X", side, 1);
        }
    };
    std::thread t1(worker, Side::BUY);
    std::thread t2(worker, Side::SELL);
    t1.join();
    t2.join();

    EXPECT_TRUE(filled.load() == 2 * PER_THREAD);
    // Equal buys and sells at a flat price net to a flat position and cash
    // unchanged (no commission configured).
    EXPECT_NEAR(gw.position("X"), 0.0, 1e-9);
    EXPECT_NEAR(gw.cash(), 10'000'000.0, 1e-6);
}

QTEST_MAIN()
