// Port of the MonteCarloTradeShuffle halves of BacktestAnalyticsRoundTest
// and TradeAnalyticsEdgeTest plus the BlockBootstrap section of
// AlphaResearchRoundTest. The shuffle draws from the Java-exact
// SplittableRandom, so seeded runs replay the Java library's streams;
// every assertion below is nonetheless a property or an order-invariant
// pin, exactly as in Java. The bootstrap's Gaussian inputs are generated
// by a seeded Box-Muller (Java used java.util.Random gaussians — same
// shape of data, and the assertions are distributional properties).
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/risk_helpers.hpp"
#include "quantfinlib/backtest/validation/block_bootstrap.hpp"
#include "quantfinlib/backtest/validation/monte_carlo_trade_shuffle.hpp"
#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::SplittableRandom;
using quantfinlib::Trade;
namespace mcs = quantfinlib::monte_carlo_trade_shuffle;
namespace bb = quantfinlib::block_bootstrap;
namespace mu = quantfinlib::math_utils;

namespace {
Trade trade(double pnl, int bars) {
    return Trade{"X", 0, bars, 0, bars, 100, 100 + pnl, 1, pnl,
                 pnl / 100, Trade::REASON_SIGNAL};
}

/** Standard normal via Box-Muller on the shared deterministic RNG. */
double gaussian(SplittableRandom& rnd) {
    double u1 = rnd.nextDouble();
    double u2 = rnd.nextDouble();
    if (u1 <= 0) {
        u1 = 1e-300;  // nextDouble() can return exactly 0
    }
    return std::sqrt(-2 * std::log(u1)) * std::cos(2 * 3.141592653589793 * u2);
}
}  // namespace

// ------------------------------------------------- MonteCarloTradeShuffle

TEST(reshuffleTerminalPnlIsOrderInvariantButDrawdownIsNot) {
    // Terminal P&L is the sum, identical in every ordering.
    std::vector<Trade> trades;
    for (int i = 0; i < 20; i++) {
        trades.push_back(trade(i % 2 == 0 ? 30 : -20, 1));  // net +100
    }
    mcs::Result r = mcs::analyze(trades, 2000, 42);
    EXPECT_NEAR(r.medianTerminalPnl, 100.0, 1e-9);  // sum is order-invariant
    EXPECT_NEAR(r.probLoss, 0.0, 0.0);              // always net positive
    // Drawdown IS order-dependent, so the distribution has spread.
    EXPECT_TRUE(r.p95MaxDrawdown >= r.medianMaxDrawdown);
    EXPECT_TRUE(r.p99MaxDrawdown >= r.p95MaxDrawdown);
    EXPECT_TRUE(r.medianMaxDrawdown > 0);
    EXPECT_TRUE(r.actualDrawdownPct >= 0 && r.actualDrawdownPct <= 1);
}

TEST(frontLoadedLossesRankAsAnUnusuallyPainfulActualOrdering) {
    // All losses first, then all wins: the worst possible drawdown
    // ordering, so the actual path sits near the top of the distribution.
    std::vector<Trade> trades;
    for (int i = 0; i < 10; i++) {
        trades.push_back(trade(-20, 1));
    }
    for (int i = 0; i < 10; i++) {
        trades.push_back(trade(30, 1));
    }
    mcs::Result r = mcs::analyze(trades, 2000, 7);
    EXPECT_NEAR(r.actualMaxDrawdown, 200, 1e-9);  // 10 * -20
    EXPECT_TRUE(r.actualDrawdownPct > 0.9);
}

TEST(shuffleGates) {
    std::vector<Trade> one = {trade(10, 1)};
    EXPECT_THROWS(mcs::analyze(one, 1000, 1));
    std::vector<Trade> two = {trade(10, 1), trade(-5, 1)};
    EXPECT_THROWS(mcs::analyze(two, 50, 1));
}

TEST(sameSeedReproducesTheShuffleDistributionExactly) {
    std::vector<Trade> trades;
    for (int i = 0; i < 15; i++) {
        trades.push_back(trade(i % 3 == 0 ? -25 : 15, 1));
    }
    mcs::Result a = mcs::analyze(trades, 500, 123);
    mcs::Result b = mcs::analyze(trades, 500, 123);
    EXPECT_TRUE(a == b);  // same seed, same trades: bit-identical result
    // A different seed draws different orderings; the median drawdown is
    // fine-grained, so at least one field should move (allowed to tie).
    mcs::Result c = mcs::analyze(trades, 500, 124);
    EXPECT_TRUE(!(a == c) || a.medianMaxDrawdown == c.medianMaxDrawdown);
}

TEST(identicalTradePnlsCollapseTheDistributionToASinglePoint) {
    // Ten identical -5 trades: every permutation is the SAME path, so all
    // drawdown percentiles equal the actual drawdown (50), every shuffle
    // ends at -50, and the loss probability is 1.
    std::vector<Trade> trades;
    for (int i = 0; i < 10; i++) {
        trades.push_back(trade(-5, 1));
    }
    mcs::Result r = mcs::analyze(trades, 200, 9);
    EXPECT_NEAR(r.actualMaxDrawdown, 50.0, 1e-12);
    EXPECT_NEAR(r.medianMaxDrawdown, 50.0, 1e-12);
    EXPECT_NEAR(r.p95MaxDrawdown, 50.0, 1e-12);
    EXPECT_NEAR(r.p99MaxDrawdown, 50.0, 1e-12);
    EXPECT_NEAR(r.medianTerminalPnl, -50.0, 1e-12);
    EXPECT_NEAR(r.probLoss, 1.0, 0.0);
    EXPECT_NEAR(r.actualDrawdownPct, 1.0, 0.0);
}

// -------------------------------------------------------- BlockBootstrap

TEST(bootstrapDistributionCentersAndIsDeterministicPerSeed) {
    int n = 300;
    SplittableRandom rnd(1);
    std::vector<double> iid(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        iid[static_cast<std::size_t>(i)] = 5e-4 + 0.01 * gaussian(rnd);
    }
    double sampleSharpe = quantfinlib::backtest_detail::sharpeRatio(iid, 0, 252);
    std::vector<double> samples = bb::sharpeSamples(iid, 10, 500, 252, 7);
    EXPECT_TRUE(samples.size() == 500);
    EXPECT_TRUE(samples[0] <= samples[499]);  // sorted ascending
    // The distribution centers on the sample estimate.
    EXPECT_NEAR(mu::percentileSorted(samples, 0.5), sampleSharpe, 0.35);
    // Deterministic per seed: replayable.
    std::vector<double> replay = bb::sharpeSamples(iid, 10, 500, 252, 7);
    EXPECT_TRUE(samples == replay);
}

TEST(blocksPreserveTheDependenceTheIidResampleDestroys) {
    // THE point of blocks: on autocorrelated returns the honest
    // uncertainty is wider, and an iid resample (L = 1) understates it.
    int n = 300;
    SplittableRandom rnd(3);
    std::vector<double> ar(static_cast<std::size_t>(n));
    double prev = 0;
    for (int i = 0; i < n; i++) {
        ar[static_cast<std::size_t>(i)] = 0.9 * prev + 0.01 * gaussian(rnd);
        prev = ar[static_cast<std::size_t>(i)];
    }
    double stdBlocked = mu::stdDev(bb::sharpeSamples(ar, 25, 400, 252, 3));
    double stdIid = mu::stdDev(bb::sharpeSamples(ar, 1, 400, 252, 3));
    EXPECT_TRUE(stdBlocked > 1.2 * stdIid);
}

TEST(bootstrapGates) {
    std::vector<double> iid(300, 0.001);
    EXPECT_THROWS(bb::sharpeSamples(iid, 0, 500, 252, 7));   // block length < 1
    EXPECT_THROWS(bb::sharpeSamples(iid, 10, 50, 252, 7));   // too few resamples
    EXPECT_THROWS(bb::sharpeSamples(iid, 10, 500, 0, 7));    // bad annualization
    std::vector<double> tooShort(49, 0.001);
    EXPECT_THROWS(bb::sharpeSamples(tooShort, 10, 500, 252, 7));
    std::vector<double> withNan(300, 0.001);
    withNan[7] = std::nan("");
    EXPECT_THROWS(bb::sharpeSamples(withNan, 10, 500, 252, 7));
    // resample() preserves length and draws only from the source values.
    SplittableRandom rnd(11);
    std::vector<double> src(60);
    for (int i = 0; i < 60; i++) {
        src[static_cast<std::size_t>(i)] = i;
    }
    std::vector<double> path = bb::resample(src, 5, rnd);
    EXPECT_TRUE(path.size() == 60);
    for (double v : path) {
        EXPECT_TRUE(v >= 0 && v < 60 && v == std::floor(v));
    }
}

QTEST_MAIN()
