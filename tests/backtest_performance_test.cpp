// Port of the BenchmarkComparison and DrawdownAnalytics sections of
// ValidationRobustnessTest.java, plus hand-pinned coverage of
// PerformanceAnalytics (the Java suite exercises it through the
// Backtester engine, which is Phase 2; the pins below are derived by
// hand in the comments).
#include <cmath>
#include <limits>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/benchmark_comparison.hpp"
#include "quantfinlib/backtest/drawdown_analytics.hpp"
#include "quantfinlib/backtest/performance_analytics.hpp"
#include "quantfinlib/backtest/risk_helpers.hpp"

using quantfinlib::PerformanceMetrics;
using quantfinlib::Trade;
namespace bc = quantfinlib::benchmark_comparison;
namespace dda = quantfinlib::drawdown_analytics;
namespace pa = quantfinlib::performance_analytics;

namespace {
Trade trade(double pnl, int bars) {
    return Trade{"X", 0, bars, 0, bars, 100, 100 + pnl, 1, pnl,
                 pnl / 100, Trade::REASON_SIGNAL};
}
}  // namespace

// ---------------------------------------------------- BenchmarkComparison

TEST(benchmarkComparisonMatchesHandComputedHalfBetaCase) {
    // Strategy = 0.5 * benchmark + 10bp: beta is exactly 0.5, alpha is
    // exactly 10bp * 252, and the capture ratios are exact fractions.
    std::vector<double> rb = {0.01, -0.01, 0.02, -0.02};
    std::vector<double> rs(4);
    for (int i = 0; i < 4; i++) {
        rs[static_cast<std::size_t>(i)] = 0.5 * rb[static_cast<std::size_t>(i)] + 0.001;
    }
    bc::Result r = bc::compare(rs, rb, 252);

    EXPECT_NEAR(r.beta, 0.5, 1e-12);
    EXPECT_NEAR(r.alpha, 0.252, 1e-12);
    EXPECT_NEAR(r.activeReturn, 0.252, 1e-12);
    // active = 0.001 - 0.5*rb -> sample var = 2.5e-4/3.
    double te = std::sqrt(2.5e-4 / 3) * std::sqrt(252.0);
    EXPECT_NEAR(r.trackingError, te, 1e-12);
    EXPECT_NEAR(r.informationRatio, 0.252 / te, 1e-12);
    EXPECT_NEAR(r.upCapture, 17.0 / 30.0, 1e-12);    // 0.0085 / 0.015
    EXPECT_NEAR(r.downCapture, 13.0 / 30.0, 1e-12);  // -0.0065 / -0.015
}

TEST(identicalSeriesHasUnitBetaZeroTrackingError) {
    std::vector<double> r = {0.01, -0.02, 0.015, 0.003, -0.007};
    bc::Result res = bc::compare(r, r, 252);
    EXPECT_NEAR(res.beta, 1.0, 1e-12);
    EXPECT_NEAR(res.alpha, 0.0, 1e-12);
    EXPECT_NEAR(res.trackingError, 0.0, 1e-12);
    EXPECT_NEAR(res.informationRatio, 0.0, 1e-12);  // 0/0 defined as 0, not NaN
}

TEST(captureIsNanNotZeroWhenBenchmarkNeverFell) {
    std::vector<double> rb = {0.01, 0.02, 0.01, 0.03};
    std::vector<double> rs = {0.005, 0.01, 0.02, 0.01};
    bc::Result r = bc::compare(rs, rb, 252);
    EXPECT_TRUE(std::isnan(r.downCapture));
    EXPECT_TRUE(std::isfinite(r.upCapture));
}

TEST(benchmarkComparisonRefusesBadInput) {
    std::vector<double> four = {0.01, -0.01, 0.02, -0.02};
    EXPECT_THROWS(bc::compare({0.1, 0.2}, four, 252));
    EXPECT_THROWS(bc::compare(four, {0.01, 0.01, 0.01, 0.01}, 252));  // no variance
    std::vector<double> withNan = {0.01, std::nan(""), 0.02, -0.02};
    EXPECT_THROWS(bc::compare(withNan, four, 252));
    EXPECT_THROWS(bc::compare(four, four, 0));
}

// ----------------------------------------------------- DrawdownAnalytics

TEST(drawdownEpisodesMatchHandWalkedCurve) {
    // Peak 110 at i=1, trough 99 at i=2 (10% deep), recovered at i=4;
    // peak 121 at i=5, trough 100 at i=7, still open at series end.
    std::vector<double> equity = {100, 110, 99, 104.5, 110, 121, 108.9, 100};
    dda::Result r = dda::analyze(equity);

    EXPECT_TRUE(r.episodes.size() == 2);
    const dda::Drawdown& first = r.episodes[0];
    EXPECT_TRUE(first.peakIndex == 1);
    EXPECT_TRUE(first.troughIndex == 2);
    EXPECT_TRUE(first.recoveryIndex == 4);
    EXPECT_NEAR(first.depth, 0.10, 1e-12);
    EXPECT_TRUE(first.duration(static_cast<int>(equity.size())) == 3);

    const dda::Drawdown& second = r.episodes[1];
    EXPECT_TRUE(second.peakIndex == 5);
    EXPECT_TRUE(second.troughIndex == 7);
    EXPECT_TRUE(second.recoveryIndex == -1);  // honest: not recovered
    EXPECT_NEAR(second.depth, 1 - 100.0 / 121.0, 1e-12);
    EXPECT_TRUE(second.duration(static_cast<int>(equity.size())) == 2);

    EXPECT_NEAR(r.maxDepth, 1 - 100.0 / 121.0, 1e-12);
    EXPECT_TRUE(r.maxDuration == 3);
    EXPECT_NEAR(r.timeUnderWater, 0.5, 1e-12);  // 4 of 8 periods below peak
    // Must agree exactly with the existing max-drawdown estimator.
    EXPECT_NEAR(quantfinlib::backtest_detail::maxDrawdown(equity), r.maxDepth, 1e-12);
}

TEST(monotonicallyRisingEquityHasNoDrawdowns) {
    dda::Result r = dda::analyze({100, 101, 105, 110});
    EXPECT_TRUE(r.episodes.empty());
    EXPECT_NEAR(r.maxDepth, 0.0, 0.0);
    EXPECT_TRUE(r.maxDuration == 0);
    EXPECT_NEAR(r.timeUnderWater, 0.0, 0.0);
}

TEST(drawdownAnalyticsRefusesNonPositiveEquity) {
    EXPECT_THROWS(dda::analyze({100}));
    EXPECT_THROWS(dda::analyze({100, 0, 50}));
    EXPECT_THROWS(dda::analyze({100, -5, 50}));
    EXPECT_THROWS(dda::analyze({100, std::numeric_limits<double>::infinity()}));
}

// ------------------------------------------------- PerformanceAnalytics

TEST(steadyGrowthPinsCagrAndZeroVolBranches) {
    // equity {100, 110, 121}, periodsPerYear = 2 (so n-1 periods = 1 year):
    // totalReturn = 0.21; cagr = 1.21^(2/2) - 1 = 0.21; rets = {0.1, 0.1}
    // -> stdDev = 0 so annVol/sharpe/sortino all take the zero branches;
    // annReturn = 0.1 * 2 = 0.2; no dips -> maxDd 0 -> calmar 0.
    std::vector<Trade> wins = {trade(10, 1), trade(11, 1)};
    PerformanceMetrics m = pa::compute({100, 110, 121}, wins, 2);
    EXPECT_NEAR(m.totalReturn, 0.21, 1e-12);
    EXPECT_NEAR(m.cagr, 0.21, 1e-12);
    EXPECT_NEAR(m.annualizedReturn, 0.2, 1e-12);
    EXPECT_NEAR(m.annualizedVolatility, 0.0, 1e-15);
    EXPECT_NEAR(m.sharpeRatio, 0.0, 0.0);
    EXPECT_NEAR(m.sortinoRatio, 0.0, 0.0);
    EXPECT_NEAR(m.maxDrawdown, 0.0, 0.0);
    EXPECT_NEAR(m.calmarRatio, 0.0, 0.0);
    // All-win record: grossLoss = 0 and grossProfit > 0 -> +inf profit factor.
    EXPECT_TRUE(std::isinf(m.profitFactor) && m.profitFactor > 0);
    EXPECT_NEAR(m.winRate, 1.0, 0.0);
    EXPECT_TRUE(m.tradeCount == 2);
    EXPECT_NEAR(m.finalEquity, 121.0, 0.0);
}

TEST(dipAndRecoveryPinsDrawdownProfitFactorAndWinRate) {
    // equity {100, 90, 99}: rets = {-0.1, +0.1}, maxDd = 10/100 = 0.1.
    // periodsPerYear = 4: annReturn = mean(0) * 4 = 0; cagr = 0.99^(4/2)-1
    // = -0.0199; calmar = cagr / 0.1. Sharpe: vol = stdDev({-.1,.1})*2 =
    // 0.1*sqrt(2)*2 -> sharpe = 0. Sortino: downside dev =
    // sqrt(0.01/2)*2, numerator 0 -> 0.
    // Trades {+30, -10, 0}: grossProfit 30, grossLoss 10 -> pf 3;
    // wins counts strictly positive only -> winRate 1/3.
    std::vector<Trade> mixed = {trade(30, 1), trade(-10, 1), trade(0, 1)};
    PerformanceMetrics m = pa::compute({100, 90, 99}, mixed, 4);
    EXPECT_NEAR(m.totalReturn, -0.01, 1e-12);
    EXPECT_NEAR(m.cagr, std::pow(0.99, 2.0) - 1, 1e-12);
    EXPECT_NEAR(m.annualizedReturn, 0.0, 1e-12);
    EXPECT_NEAR(m.maxDrawdown, 0.1, 1e-12);
    EXPECT_NEAR(m.calmarRatio, (std::pow(0.99, 2.0) - 1) / 0.1, 1e-12);
    EXPECT_NEAR(m.sharpeRatio, 0.0, 1e-12);
    EXPECT_NEAR(m.profitFactor, 3.0, 1e-12);
    EXPECT_NEAR(m.winRate, 1.0 / 3, 1e-12);
    EXPECT_TRUE(m.tradeCount == 3);
}

TEST(singlePointCurveAndEmptyInputs) {
    // A single equity point: no returns -> every ratio takes its zero
    // branch; totalReturn = 0; cagr guard (n > 1) leaves 0.
    PerformanceMetrics m = pa::compute({100}, {}, 252);
    EXPECT_NEAR(m.totalReturn, 0.0, 0.0);
    EXPECT_NEAR(m.cagr, 0.0, 0.0);
    EXPECT_NEAR(m.annualizedReturn, 0.0, 0.0);
    EXPECT_NEAR(m.sharpeRatio, 0.0, 0.0);
    // No trades: profitFactor 0 (no gross profit either), winRate 0.
    EXPECT_NEAR(m.profitFactor, 0.0, 0.0);
    EXPECT_NEAR(m.winRate, 0.0, 0.0);
    EXPECT_TRUE(m.tradeCount == 0);
    // Added gate versus Java (which would index out of bounds).
    EXPECT_THROWS(pa::compute({}, {}, 252));
}

QTEST_MAIN()
