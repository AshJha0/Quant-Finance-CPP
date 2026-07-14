// Phase 5B execution tests: the stateful/decision algos --
// BenchmarkExecutor, LiquiditySeekingAlgo, SpreadExecutionAlgo,
// FuturesRollAlgo, AntiGamingJitter, OrderPlacementPolicy, and
// PortfolioExecutor. Pins are ported 1:1 from the Java reference tests
// (BenchmarkExecutorTest, LiquiditySeekingAlgoTest,
// AdvancedExecutionAlgosTest, PortfolioExecutorTest).
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/execution/anti_gaming_jitter.hpp"
#include "quantfinlib/execution/benchmark_executor.hpp"
#include "quantfinlib/execution/futures_roll_algo.hpp"
#include "quantfinlib/execution/liquidity_seeking_algo.hpp"
#include "quantfinlib/execution/order_placement_policy.hpp"
#include "quantfinlib/execution/portfolio_executor.hpp"
#include "quantfinlib/execution/spread_execution_algo.hpp"
#include "quantfinlib/orderbook/side.hpp"

using namespace quantfinlib;

namespace {
BenchmarkExecutor::MarketState neutral(double mid, double frac) {
    return BenchmarkExecutor::MarketState::neutral(mid, frac);
}
}  // namespace

// ------------------------------------------------------------- BenchmarkExecutor

TEST(benchmarkExecutorTwapIsLinearInTime) {
    BenchmarkExecutor e(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP, 0.1, 0, 1.0);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(100, 0.5)) == 5000);
    e.onFill(5000);
    EXPECT_TRUE(e.dueQuantity(0.5, neutral(100, 0.5)) == 0);
    EXPECT_TRUE(e.dueQuantity(1.0, neutral(100, 1.0)) == 5000);
}

TEST(benchmarkExecutorVwapTracksVolumeCurveNotWallClock) {
    BenchmarkExecutor e(Side::BUY, 100000, BenchmarkExecutor::Benchmark::VWAP, 0.1, 0, 1.0);
    // 20% of time elapsed but 50% of expected volume already traded (busy
    // open): VWAP should be ~50% done, not 20%.
    BenchmarkExecutor::MarketState busyOpen{100, 0, 0, std::numeric_limits<double>::infinity(), 0.5, 0, 0};
    EXPECT_TRUE(e.dueQuantity(0.2, busyOpen) == 50000);
}

TEST(benchmarkExecutorParticipationChasesRealizedVolume) {
    BenchmarkExecutor e = BenchmarkExecutor::pov(Side::BUY, 1000000, 0.10);
    EXPECT_TRUE(e.dueQuantity(0.0, neutral(100, 0.0)) == 0);
    e.onMarketVolume(500000);  // target = 50k
    EXPECT_TRUE(e.dueQuantity(0.0, neutral(100, 0.0)) == 50000);
    e.onFill(50000);
    e.onMarketVolume(300000);  // target 80k, done 50k
    std::int64_t due = e.dueQuantity(0.0, neutral(100, 0.0));
    EXPECT_TRUE(due == 30000);
    e.onFill(due);
    EXPECT_NEAR(e.realizedParticipation(), 0.10, 1e-9);
}

TEST(benchmarkExecutorParticipationSingleCapRegression) {
    // The participation rate is a hard promise: even with alpha pushing
    // urgency toward 4x, a POV child must never exceed the plain
    // behind-schedule quantity -- urgency is clamped to <= 1 for
    // PARTICIPATION only (unlike every other benchmark).
    BenchmarkExecutor e(Side::BUY, 1000000, BenchmarkExecutor::Benchmark::PARTICIPATION, 0.10, 10, 1.0);
    e.onMarketVolume(500000);  // target = 50,000
    BenchmarkExecutor::MarketState m{100, 0, 0, std::numeric_limits<double>::infinity(), 0.5, 1.0, 0};
    std::int64_t due = e.dueQuantity(0.0, m);
    EXPECT_TRUE(due == 50000);
}

TEST(benchmarkExecutorLiquidityCapLimitsTheChild) {
    BenchmarkExecutor e(Side::BUY, 100000, BenchmarkExecutor::Benchmark::TWAP, 0.1, 0, 0.25);
    // Behind by ~50k, but only 40k displayed -> capped at 25% x 40k = 10k.
    BenchmarkExecutor::MarketState thin{100, 0, 0, 40000, 0.5, 0, 0};
    EXPECT_TRUE(e.dueQuantity(0.5, thin) == 10000);
}

TEST(benchmarkExecutorNanMarketInputsAreNeutralNotASilentStall) {
    BenchmarkExecutor e(Side::BUY, 100000, BenchmarkExecutor::Benchmark::TWAP, 0.1, 10, 1.0);
    double nan = std::numeric_limits<double>::quiet_NaN();
    BenchmarkExecutor::MarketState poisoned{100, nan, nan, std::numeric_limits<double>::infinity(), 0.5, nan, 0};
    EXPECT_TRUE(e.dueQuantity(0.5, poisoned) == 50000);

    BenchmarkExecutor v(Side::BUY, 100000, BenchmarkExecutor::Benchmark::VWAP, 0.1, 0, 1.0);
    BenchmarkExecutor::MarketState nanVol{100, 0, 0, std::numeric_limits<double>::infinity(), nan, 0, 0};
    EXPECT_TRUE(v.dueQuantity(0.5, nanVol) == 50000);
}

TEST(benchmarkExecutorValidatesInputsAndOfRejectsParticipation) {
    EXPECT_THROWS(BenchmarkExecutor::of(Side::BUY, 1000, BenchmarkExecutor::Benchmark::PARTICIPATION));
    EXPECT_THROWS(BenchmarkExecutor(Side::BUY, 0, BenchmarkExecutor::Benchmark::TWAP, 0.1, 10, 0.25));
    EXPECT_THROWS(BenchmarkExecutor::pov(Side::BUY, 1000, 1.5));
    EXPECT_THROWS(BenchmarkExecutor(Side::BUY, 1000, BenchmarkExecutor::Benchmark::TWAP, 0.1, -1, 0.25));
    EXPECT_THROWS(BenchmarkExecutor(Side::BUY, 1000, BenchmarkExecutor::Benchmark::TWAP, 0.1, 10, 0));
}

// --------------------------------------------------------- LiquiditySeekingAlgo

namespace {
BenchmarkExecutor::MarketState seekState(double spread, double vol, double depth, double impactBps) {
    return BenchmarkExecutor::MarketState{100, spread, vol, depth, 0.5, 0, impactBps};
}
}  // namespace

TEST(liquiditySeekingCheapCalmMomentTriggersAnAggressiveClip) {
    LiquiditySeekingAlgo seek(100000);
    std::int64_t due = seek.dueQuantity(0.3, seekState(0.015, 0.2, 40000, 2), 0.02);
    EXPECT_TRUE(due == 10000);  // 25% of displayed depth, taken now
}

TEST(liquiditySeekingExpensiveOrVolatileMomentWaits) {
    LiquiditySeekingAlgo seek(100000);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.03, 0.2, 40000, 2), 0.02) == 0);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.015, 0.8, 40000, 2), 0.02) == 0);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.015, 0.2, 40000, 10), 0.02) == 0);
}

TEST(liquiditySeekingCompletionFloorMakesPatienceSafe) {
    LiquiditySeekingAlgo seek(100000);
    BenchmarkExecutor::MarketState expensive = seekState(0.05, 0.9, 40000, 20);  // never cheap
    EXPECT_TRUE(seek.dueQuantity(0.69, expensive, 0.02) == 0);
    std::int64_t half = seek.dueQuantity(0.85, expensive, 0.02);
    EXPECT_TRUE(half == 50000);
    seek.onFill(half);
    EXPECT_TRUE(seek.dueQuantity(1.0, expensive, 0.02) == 50000);
    seek.onFill(50000);
    EXPECT_TRUE(seek.done());
    EXPECT_TRUE(seek.dueQuantity(1.0, expensive, 0.02) == 0);
}

TEST(liquiditySeekingUnknowableInputsAreNeverCheapButAlwaysComplete) {
    LiquiditySeekingAlgo seek(100000);
    double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.015, 0.2, 40000, 2), nan) == 0);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(nan, 0.2, 40000, 2), 0.02) == 0);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.015, nan, 40000, 2), 0.02) == 0);
    EXPECT_TRUE(seek.dueQuantity(0.3, seekState(0.015, 0.2, 40000, nan), 0.02) == 0);
    EXPECT_TRUE(seek.dueQuantity(1.0, seekState(nan, nan, nan, nan), nan) == 100000);
    LiquiditySeekingAlgo deep(5000);
    EXPECT_TRUE(deep.dueQuantity(0.1, seekState(0.015, 0.2, std::numeric_limits<double>::infinity(), 0), 0.02)
                == 5000);
    EXPECT_THROWS(LiquiditySeekingAlgo(0));
    EXPECT_THROWS(LiquiditySeekingAlgo::Config(0.1, 0.5, 5, 0.25, 1.0));
}

// ----------------------------------------------------------- SpreadExecutionAlgo

TEST(spreadExecutionHedgeLegChasesAndTheLeggingCapIsHard) {
    // 10k lead units, 2 hedge units per lead, cap 3k hedge units, patient
    // 1k lead children.
    SpreadExecutionAlgo spread(10000, 2.0, 3000, 1000);

    SpreadExecutionAlgo::Children c = spread.decide();
    EXPECT_TRUE(c.leadQty == 1000);
    EXPECT_TRUE(c.hedgeQty == 0);
    EXPECT_TRUE(!c.atRiskCap);

    // Lead fills, hedge does not: the imbalance is 2,000 hedge units.
    spread.onLeadFill(1000);
    c = spread.decide();
    EXPECT_TRUE(c.hedgeQty == 2000);
    EXPECT_TRUE(c.leadQty == 500);  // (3000-2000)/2

    // The hedge keeps not filling; lead fills again: cap reached.
    spread.onLeadFill(500);
    c = spread.decide();
    EXPECT_TRUE(c.atRiskCap);
    EXPECT_TRUE(c.leadQty == 0);
    EXPECT_TRUE(c.hedgeQty == 3000);

    // Hedge catches up: lead resumes.
    spread.onHedgeFill(3000);
    EXPECT_TRUE(spread.imbalanceHedgeUnits() == 0);
    c = spread.decide();
    EXPECT_TRUE(c.leadQty == 1000);
    EXPECT_TRUE(!spread.done());

    // Run to completion: done only when the hedge has fully caught up.
    spread.onLeadFill(8500);
    EXPECT_TRUE(!spread.done());
    spread.onHedgeFill(spread.imbalanceHedgeUnits());
    EXPECT_TRUE(spread.done());
    EXPECT_TRUE(spread.hedgeExecuted() == 20000);

    EXPECT_THROWS(spread.onLeadFill(1));
    EXPECT_THROWS(SpreadExecutionAlgo(0, 2, 3000, 100));
    EXPECT_THROWS(SpreadExecutionAlgo(100, std::numeric_limits<double>::quiet_NaN(), 3000, 100));
}

TEST(spreadExecutionFractionalRatiosAndImpossibleCapsAreHandled) {
    // Ratio 1.5, lead 3: the hedge target is round(4.5) = 5 -- half-up
    // over-hedging by design.
    SpreadExecutionAlgo frac(3, 1.5, 4, 3);
    while (!frac.done()) {
        SpreadExecutionAlgo::Children c = frac.decide();
        frac.onLeadFill(c.leadQty);
        frac.onHedgeFill(c.hedgeQty);
    }
    EXPECT_TRUE(frac.leadExecuted() == 3);
    EXPECT_TRUE(frac.hedgeExecuted() == 5);

    // A legging cap smaller than one lead unit's hedge would make
    // execution IMPOSSIBLE: rejected at construction.
    EXPECT_THROWS(SpreadExecutionAlgo(100, 2.0, 1, 10));

    SpreadExecutionAlgo over(10, 1.0, 5, 10);
    over.onLeadFill(10);
    EXPECT_THROWS(over.onHedgeFill(11));
    over.onHedgeFill(10);
    EXPECT_TRUE(over.done());
}

// -------------------------------------------------------------- FuturesRollAlgo

TEST(futuresRollFollowsTheMigrationCurveAndAlwaysCompletes) {
    std::vector<double> curve = FuturesRollAlgo::defaultMigration(5);
    EXPECT_NEAR(curve[4], 1.0, 0.0);
    for (int d = 1; d < 5; d++) {
        EXPECT_TRUE(curve[static_cast<std::size_t>(d)] > curve[static_cast<std::size_t>(d - 1)]);
    }
    EXPECT_TRUE(curve[2] - curve[1] > curve[0]);  // concentrated middle

    FuturesRollAlgo roll(10000, curve);
    std::int64_t day0 = roll.dueOnDay(0);
    EXPECT_TRUE(day0 == static_cast<std::int64_t>(std::llround(10000 * curve[0])));
    roll.onRolled(day0);

    // Desk pulled away on day 1: day 2 catches up to the curve.
    EXPECT_TRUE(roll.dueOnDay(2) == static_cast<std::int64_t>(std::llround(10000 * curve[2])) - day0);
    roll.onRolled(roll.dueOnDay(2));
    roll.onRolled(roll.dueOnDay(3));
    roll.onRolled(roll.dueOnDay(4));
    EXPECT_TRUE(roll.done());
    EXPECT_TRUE(roll.rolled() == 10000);

    // Each day's due executes as a calendar spread: ratio 1, tight cap.
    SpreadExecutionAlgo calendar(day0, 1.0, 200, 300);
    while (!calendar.done()) {
        SpreadExecutionAlgo::Children c = calendar.decide();
        calendar.onLeadFill(c.leadQty);
        calendar.onHedgeFill(c.hedgeQty);
    }
    EXPECT_TRUE(calendar.leadExecuted() == calendar.hedgeExecuted());

    EXPECT_THROWS(FuturesRollAlgo(1000, std::vector<double>{0.5, 0.9}));
    EXPECT_THROWS(FuturesRollAlgo(1000, std::vector<double>{0.5, 0.4, 1.0}));
    EXPECT_THROWS(roll.onRolled(1));

    FuturesRollAlgo oneDay(500, FuturesRollAlgo::defaultMigration(1));
    EXPECT_TRUE(oneDay.dueOnDay(0) == 500);
    oneDay.onRolled(500);
    EXPECT_TRUE(oneDay.done());

    FuturesRollAlgo oneLot(1, FuturesRollAlgo::defaultMigration(5));
    std::int64_t total = 0;
    for (int d = 0; d < 5; d++) {
        std::int64_t due = oneLot.dueOnDay(d);
        oneLot.onRolled(due);
        total += due;
    }
    EXPECT_TRUE(total == 1);
    EXPECT_TRUE(oneLot.done());

    FuturesRollAlgo lateStart(1000, std::vector<double>{0, 0, 1});
    EXPECT_TRUE(lateStart.dueOnDay(0) == 0);
    EXPECT_TRUE(lateStart.dueOnDay(2) == 1000);
}

// -------------------------------------------------------------- AntiGamingJitter

TEST(antiGamingJitterKillsThePatternButNeverTheTotal) {
    std::vector<std::int64_t> clockwork(12, 1000);

    AntiGamingJitter jitter(42, 0.3, 0.4);
    std::vector<std::int64_t> jittered = jitter.jitterSizes(clockwork);

    std::int64_t total = 0;
    bool anyDifferent = false;
    for (std::int64_t q : jittered) {
        total += q;
        EXPECT_TRUE(q >= 0);
        anyDifferent |= (q != 1000);
    }
    EXPECT_TRUE(total == 12000);
    EXPECT_TRUE(anyDifferent);

    // Deterministic per seed.
    std::vector<std::int64_t> replay = AntiGamingJitter(42, 0.3, 0.4).jitterSizes(clockwork);
    EXPECT_TRUE(jittered == replay);
    std::vector<std::int64_t> differentSeed = AntiGamingJitter(43, 0.3, 0.4).jitterSizes(clockwork);
    EXPECT_TRUE(jittered != differentSeed);

    // Times: monotone, inside the window, never past the original end.
    std::vector<std::int64_t> times(10);
    for (int i = 0; i < 10; i++) {
        times[static_cast<std::size_t>(i)] = (i + 1) * 60000000000LL;
    }
    std::vector<std::int64_t> jt = AntiGamingJitter(7, 0, 0.4).jitterTimes(times, 0);
    for (std::size_t i = 1; i < jt.size(); i++) {
        EXPECT_TRUE(jt[i] > jt[i - 1]);
    }
    EXPECT_TRUE(jt.front() >= 0);
    EXPECT_TRUE(jt.back() <= times.back());
    EXPECT_TRUE(jt != times);

    EXPECT_THROWS(AntiGamingJitter(1, 0.6, 0.1));
    EXPECT_THROWS(jitter.jitterTimes(std::vector<std::int64_t>{5, 5}, 0));

    // Zero fractions = IDENTITY, exactly.
    AntiGamingJitter off(99, 0, 0);
    EXPECT_TRUE(off.jitterSizes(clockwork) == clockwork);
    EXPECT_TRUE(off.jitterTimes(times, 0) == times);

    // Degenerate schedules survive: one child, no pairs to transfer.
    EXPECT_TRUE(jitter.jitterSizes(std::vector<std::int64_t>{5000}) == std::vector<std::int64_t>{5000});
    EXPECT_TRUE(AntiGamingJitter(3, 0.3, 0.4).jitterTimes(std::vector<std::int64_t>{100}, 0)
                == std::vector<std::int64_t>{100});
}

// ------------------------------------------------------------ OrderPlacementPolicy

TEST(orderPlacementPolicyFollowsExpectedCostExactly) {
    // Friendly conditions: 5c half spread, 60% fill odds, mild 2c adverse
    // selection, 1c drift, 0.2c rebate.
    order_placement_policy::Placement friendly = order_placement_policy::decide(0.05, 0.6, 0.02, 0.01, 0.002);
    EXPECT_NEAR(friendly.expectedPostCost, 0.0048, 1e-12);
    EXPECT_NEAR(friendly.crossCost, 0.05, 1e-12);
    EXPECT_TRUE(friendly.post);

    // Toxic tape: 15c adverse selection swamps everything.
    order_placement_policy::Placement toxic = order_placement_policy::decide(0.05, 0.6, 0.15, 0.01, 0.002);
    EXPECT_NEAR(toxic.expectedPostCost, 0.0828, 1e-12);
    EXPECT_TRUE(!toxic.post);

    // The post REGION, verified from both sides. Normal regime
    // (2h+r+d > a): posting pays ABOVE the threshold.
    order_placement_policy::PostRegion normal = order_placement_policy::postRegion(0.05, 0.02, 0.01, 0.002);
    EXPECT_NEAR(normal.from, 0.01 / 0.092, 1e-12);
    EXPECT_NEAR(normal.to, 1, 1e-12);
    EXPECT_TRUE(order_placement_policy::decide(0.05, normal.from + 0.01, 0.02, 0.01, 0.002).post);
    EXPECT_TRUE(!order_placement_policy::decide(0.05, normal.from - 0.01, 0.02, 0.01, 0.002).post);

    // TOXIC regime (a > 2h+r+d): the slope flips and posting never pays --
    // the region is EMPTY.
    EXPECT_TRUE(order_placement_policy::postRegion(0.05, 0.15, 0.01, 0.002).isEmpty());

    // FLIPPED regime with favorable drift -- the flipped-inequality pin:
    // posting pays only BELOW the threshold (waiting is good, but a fill
    // means being run over).
    order_placement_policy::PostRegion flipped = order_placement_policy::postRegion(0.05, 0.3, -0.05, 0);
    EXPECT_NEAR(flipped.from, 0, 1e-12);
    EXPECT_NEAR(flipped.to, 0.2, 1e-12);  // d/coef = -0.05/-0.25
    EXPECT_TRUE(order_placement_policy::decide(0.05, 0.1, 0.3, -0.05, 0).post);
    EXPECT_TRUE(!order_placement_policy::decide(0.05, 0.3, 0.3, -0.05, 0).post);

    // Flat-in-p regime (coef == 0): the drift sign decides for all p.
    EXPECT_TRUE(!order_placement_policy::postRegion(1, 1, -1, 0).isEmpty());
    EXPECT_TRUE(order_placement_policy::postRegion(1, 2.1, 0.1, 0).isEmpty());

    // Exact probability boundaries.
    EXPECT_TRUE(!order_placement_policy::decide(0.05, 0, 0.02, 0, 0).post);
    EXPECT_NEAR(order_placement_policy::decide(0.05, 1, 0.02, 0.01, 0.002).expectedPostCost,
               0.02 - 0.05 - 0.002, 1e-12);

    EXPECT_TRUE(order_placement_policy::decide(0.05, 0.1, 0.02, -0.03, 0).post);

    EXPECT_THROWS(order_placement_policy::decide(0, 0.5, 0.02, 0.01, 0));
    EXPECT_THROWS(order_placement_policy::decide(0.05, 1.5, 0.02, 0.01, 0));
    EXPECT_THROWS(order_placement_policy::decide(0.05, 0.5, 0.02, std::numeric_limits<double>::quiet_NaN(), 0));
}

// -------------------------------------------------------------- PortfolioExecutor

TEST(portfolioExecutorUnconstrainedIsATransparentPassthrough) {
    PortfolioExecutor pe(2, PortfolioExecutor::Config::unconstrained());
    int buy = pe.add(BenchmarkExecutor::of(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP));
    int sell = pe.add(BenchmarkExecutor::of(Side::SELL, 6000, BenchmarkExecutor::Benchmark::TWAP));

    std::vector<BenchmarkExecutor::MarketState> states = {neutral(100, 0.5), neutral(50, 0.5)};
    std::vector<std::int64_t> due(2);
    pe.decide(0.5, states, due);

    EXPECT_TRUE(due[static_cast<std::size_t>(buy)] == 5000);
    EXPECT_TRUE(due[static_cast<std::size_t>(sell)] == 3000);
}

TEST(portfolioExecutorLegBalanceStopsTheAheadLegWhenTheOtherIsStuck) {
    PortfolioExecutor::Config config(100000, std::numeric_limits<double>::infinity());
    PortfolioExecutor pe(2, config);
    int buy = pe.add(BenchmarkExecutor::of(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP));
    int sell = pe.add(BenchmarkExecutor::of(Side::SELL, 10000, BenchmarkExecutor::Benchmark::TWAP));

    pe.onFill(buy, 5000, 100);
    EXPECT_NEAR(pe.netNotional(), 500000, 1e-9);

    std::vector<std::int64_t> due(2);
    BenchmarkExecutor::MarketState buyState = neutral(100, 0.6);
    BenchmarkExecutor::MarketState sellState{100, 0, 0, 0, 0.6, 0, 0};  // no depth
    pe.decide(0.6, {buyState, sellState}, due);

    EXPECT_TRUE(due[static_cast<std::size_t>(sell)] == 0);
    EXPECT_TRUE(due[static_cast<std::size_t>(buy)] == 0);

    // Prove the band (not the child) is what stopped it.
    PortfolioExecutor free(2, PortfolioExecutor::Config::unconstrained());
    int b2 = free.add(BenchmarkExecutor::of(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP));
    free.add(BenchmarkExecutor::of(Side::SELL, 10000, BenchmarkExecutor::Benchmark::TWAP));
    free.onFill(b2, 5000, 100);
    std::vector<std::int64_t> due2(2);
    free.decide(0.6, {buyState, sellState}, due2);
    EXPECT_TRUE(due2[static_cast<std::size_t>(b2)] == 1000);
}

TEST(portfolioExecutorCapacityAllocationCannotReviolateTheLegBand) {
    // Regression: the risk-weighted capacity cut hits the two legs
    // asymmetrically, so it can push |net| back over the band the first
    // leg-balance pass enforced -- the band must be re-applied after it.
    PortfolioExecutor::Config config(100000, 1000000);
    PortfolioExecutor pe(2, config);
    int buy = pe.add(BenchmarkExecutor::of(Side::BUY, 20000, BenchmarkExecutor::Benchmark::TWAP));
    int sell = pe.add(BenchmarkExecutor::of(Side::SELL, 20000, BenchmarkExecutor::Benchmark::TWAP));

    // Calm buy leg, wild sell leg (vol regime 1 -> sell child self-damps
    // AND earns double risk weight when the budget binds).
    BenchmarkExecutor::MarketState buyState = neutral(100, 0.5);
    BenchmarkExecutor::MarketState sellState{100, 0, 1.0, std::numeric_limits<double>::infinity(), 0.5, 0, 0};
    std::vector<std::int64_t> due(2);
    pe.decide(0.5, {buyState, sellState}, due);

    // Band pass 1: buys 1M vs sells 500k -> buys scaled to 600k (6,000).
    // Capacity: total 1.1M > 1M; weights buy 600k, sell 1M -> buy cut to
    // 375k (3,750), sell keeps 500k -> net now -125k: RE-VIOLATED.
    // Band pass 2: sells scaled to 475k (4,750) -> net exactly -100k.
    EXPECT_TRUE(due[static_cast<std::size_t>(buy)] == 3750);
    EXPECT_TRUE(due[static_cast<std::size_t>(sell)] == 4750);
    double projectedNet = static_cast<double>(due[static_cast<std::size_t>(buy)]) * 100.0
            - static_cast<double>(due[static_cast<std::size_t>(sell)]) * 100.0;
    EXPECT_TRUE(std::abs(projectedNet) <= 100000);
    EXPECT_TRUE(static_cast<double>(due[static_cast<std::size_t>(buy)]) * 100.0
                        + static_cast<double>(due[static_cast<std::size_t>(sell)]) * 100.0
                <= 1000000);
}

TEST(portfolioExecutorCapacityGoesToTheRiskiestNamesWhenTheBudgetBinds) {
    PortfolioExecutor::Config config(std::numeric_limits<double>::infinity(), 500000);
    PortfolioExecutor pe(2, config);
    int calm = pe.add(BenchmarkExecutor::of(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP));
    int wild = pe.add(BenchmarkExecutor::of(Side::BUY, 10000, BenchmarkExecutor::Benchmark::TWAP));

    BenchmarkExecutor::MarketState calmState = neutral(100, 0.5);
    BenchmarkExecutor::MarketState wildState{100, 0, 1.0, std::numeric_limits<double>::infinity(), 0.5, 0, 0};
    std::vector<std::int64_t> due(2);
    pe.decide(0.5, {calmState, wildState}, due);

    EXPECT_TRUE(due[static_cast<std::size_t>(wild)] == 2500);
    EXPECT_TRUE(due[static_cast<std::size_t>(calm)] == 2500);
    EXPECT_TRUE(due[static_cast<std::size_t>(calm)] * 100.0 + due[static_cast<std::size_t>(wild)] * 100.0 <= 500000);
}

TEST(portfolioExecutorFillsFlowThroughToChildrenAndTheLedger) {
    PortfolioExecutor pe(2, PortfolioExecutor::Config::unconstrained());
    int buy = pe.add(BenchmarkExecutor::of(Side::BUY, 1000, BenchmarkExecutor::Benchmark::TWAP));
    int sell = pe.add(BenchmarkExecutor::of(Side::SELL, 1000, BenchmarkExecutor::Benchmark::TWAP));

    pe.onFill(buy, 1000, 100);
    pe.onFill(sell, 400, 50);
    EXPECT_TRUE(pe.child(buy).executed() == 1000);
    EXPECT_NEAR(pe.netNotional(), 100000 - 20000, 1e-9);
    EXPECT_TRUE(pe.child(buy).done());
    EXPECT_TRUE(!pe.done());
    pe.onFill(sell, 600, 50);
    EXPECT_TRUE(pe.done());

    // A NaN fill price advances the child but cannot poison the ledger.
    PortfolioExecutor pe2(1, PortfolioExecutor::Config::unconstrained());
    int h = pe2.add(BenchmarkExecutor::of(Side::BUY, 100, BenchmarkExecutor::Benchmark::TWAP));
    pe2.onFill(h, 100, std::numeric_limits<double>::quiet_NaN());
    EXPECT_TRUE(pe2.child(h).executed() == 100);
    EXPECT_NEAR(pe2.netNotional(), 0, 1e-9);
}

TEST(portfolioExecutorValidation) {
    EXPECT_THROWS(PortfolioExecutor::Config(0, 1));
    EXPECT_THROWS(PortfolioExecutor::Config(1, std::numeric_limits<double>::quiet_NaN()));
    EXPECT_THROWS(PortfolioExecutor(0, PortfolioExecutor::Config::unconstrained()));

    PortfolioExecutor pe(1, PortfolioExecutor::Config::unconstrained());
    pe.add(BenchmarkExecutor::of(Side::BUY, 100, BenchmarkExecutor::Benchmark::TWAP));
    EXPECT_THROWS(pe.add(BenchmarkExecutor::of(Side::BUY, 100, BenchmarkExecutor::Benchmark::TWAP)));
    std::vector<BenchmarkExecutor::MarketState> empty;
    std::vector<std::int64_t> oneDue(1);
    EXPECT_THROWS(pe.decide(0.5, empty, oneDue));
}

QTEST_MAIN()
