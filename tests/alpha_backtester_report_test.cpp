// Port of the alpha.AlphaBacktesterReportTest pins (Java): the
// execution-aware backtest (sqrt-impact cost decomposition, capital
// scaling, capacity failure) and the reporting layer (alpha decay,
// exact OLS attribution, drawdown/rolling-Sharpe curves).
#include "quantfinlib/alpha/alpha_backtester.hpp"
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_report.hpp"
#include "quantfinlib/alpha/factors.hpp"
#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/risk/java_random.hpp"

#include <cmath>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AlphaContext;
using quantfinlib::BarSeries;
namespace factors = quantfinlib::factors;
namespace bt = quantfinlib::alpha_backtester;
namespace rep = quantfinlib::alpha_report;
namespace pc = quantfinlib::portfolio_construction;

namespace {

constexpr int BARS = 260;
const std::vector<double> DRIFTS = {0.004, 0.002, 0.001, -0.001, -0.002, -0.004};

AlphaContext panel() {
    std::unordered_map<std::string, BarSeries> data;
    for (std::size_t s = 0; s < DRIFTS.size(); s++) {
        auto b = BarSeries::builder("S" + std::to_string(s));
        double close = 100;
        for (int i = 0; i < BARS; i++) {
            double open = close;
            close = 100 * std::pow(1 + DRIFTS[s], i + 1);
            b.add(i, open, std::max(open, close), std::min(open, close), close, 500'000);
        }
        data.emplace("S" + std::to_string(s), b.build());
    }
    return AlphaContext::of(data);
}

AlphaContext noisyPanel() {
    quantfinlib::JavaRandom rng(99);
    std::unordered_map<std::string, BarSeries> data;
    for (std::size_t s = 0; s < DRIFTS.size(); s++) {
        auto b = BarSeries::builder("S" + std::to_string(s));
        double close = 100;
        for (int i = 0; i < BARS; i++) {
            double open = close;
            close = open * (1 + DRIFTS[s] + 0.004 * (rng.nextDouble() - 0.5));
            b.add(i, open, std::max(open, close), std::min(open, close), close, 500'000);
        }
        data.emplace("S" + std::to_string(s), b.build());
    }
    return AlphaContext::of(data);
}

bt::Config configOf(int start, double capital) {
    bt::Config c;
    c.startIndex = start;
    c.rebalanceEveryBars = 21;
    c.commissionBps = 1.0;
    c.halfSpreadBps = 2.0;
    c.slippageBps = 1.0;
    c.capital = capital;
    c.impactWindow = 20;
    c.periodsPerYear = 252;
    return c.withIdealisedExecution();
}

}  // namespace

TEST(predictiveFactorProfitsAndCostsBite) {
    AlphaContext ctx = noisyPanel();
    bt::Config config = configOf(30, 100'000'000);
    bt::Result r = bt::run(ctx, factors::momentum(20, 0), config);

    double grossFinal = r.grossEquity.back();
    double netFinal = r.netEquity.back();
    EXPECT_TRUE(grossFinal > 1.0);
    EXPECT_TRUE(netFinal < grossFinal);
    EXPECT_TRUE(r.commissionDrag > 0);
    EXPECT_TRUE(r.spreadDrag > 0);
    EXPECT_TRUE(r.slippageDrag > 0);
    EXPECT_TRUE(r.impactDrag > 0);
    EXPECT_NEAR(r.commissionDrag + r.spreadDrag + r.slippageDrag + r.impactDrag, r.totalCostDrag(), 1e-12);
    EXPECT_TRUE(r.netMetrics.sharpeRatio > 0);
    EXPECT_TRUE(r.grossMetrics.sharpeRatio >= r.netMetrics.sharpeRatio);
    EXPECT_TRUE(r.meanTurnover < 0.5);
}

TEST(impactScalesWithCapitalAndDisablesAtZero) {
    AlphaContext ctx = noisyPanel();
    bt::Config small = configOf(30, 0);
    bt::Config big = configOf(30, 1'000'000'000);
    bt::Result noImpact = bt::run(ctx, factors::momentum(20, 0), small);
    bt::Result bigBook = bt::run(ctx, factors::momentum(20, 0), big);
    EXPECT_NEAR(noImpact.impactDrag, 0.0, 0.0);
    EXPECT_TRUE(bigBook.impactDrag > 0);
    EXPECT_TRUE(bigBook.netEquity.back() < noImpact.netEquity.back());

    bt::Config bad = configOf(-1, 0);
    EXPECT_THROWS(bt::validateConfig(bad));
    bt::Config endStart = configOf(BARS - 1, 0);
    EXPECT_THROWS(bt::run(ctx, factors::momentum(20, 0), endStart));
}

TEST(impactPreconditionAndOversizedBooksFailLoud) {
    AlphaContext ctx = noisyPanel();
    bt::Config defaults10 = bt::Config::defaults(10);
    bool threw1 = false;
    try {
        bt::run(ctx, factors::momentum(5, 0), defaults10);
    } catch (const std::invalid_argument& e) {
        threw1 = true;
        EXPECT_TRUE(std::string(e.what()).find("impactWindow") != std::string::npos);
    }
    EXPECT_TRUE(threw1);

    bt::Config huge = configOf(30, 1e18);
    bool threw2 = false;
    try {
        bt::run(ctx, factors::momentum(20, 0), huge);
    } catch (const std::invalid_argument& e) {
        threw2 = true;
        EXPECT_TRUE(std::string(e.what()).find("liquidity") != std::string::npos);
    }
    EXPECT_TRUE(threw2);
}

TEST(attributionRejectsNaNInputsAndSharpesAgree) {
    std::vector<double> y = {0.01, 0.02, -0.01, 0.005, 0.0, 0.01};
    std::vector<double> f = {0.01, std::nan(""), -0.01, 0.005, 0.0, 0.01};
    bool threw = false;
    try {
        rep::attribute(y, {f}, {"MOM"});
    } catch (const std::invalid_argument& e) {
        threw = true;
        std::string msg = e.what();
        EXPECT_TRUE(msg.find("MOM") != std::string::npos && msg.find('1') != std::string::npos);
    }
    EXPECT_TRUE(threw);

    std::vector<double> equity = {1.0, 1.02, 1.01, 1.05, 1.04, 1.08, 1.07, 1.12};
    std::vector<double> returns = rep::returnsOf(equity);
    std::vector<double> rolling = rep::rollingSharpe(returns, static_cast<int>(returns.size()), 252);
    EXPECT_NEAR(rep::summarize(equity, 252).sharpeRatio, rolling.back(), 1e-9);
}

TEST(customConstructionPipelinePlugsIn) {
    AlphaContext ctx = noisyPanel();
    bt::Config config = configOf(30, 0);
    bt::Result r = bt::run(ctx, factors::momentum(20, 0), config,
                           [](const AlphaContext& c, const std::vector<double>& scores, int index) {
                               std::vector<double> w = pc::zScoreWeights(scores, 1.0, 0.5);
                               w = pc::betaNeutralize(w, pc::trailingBetas(c, index, 25));
                               return pc::inverseVolBudget(w, pc::trailingVols(c, index, 25), 1.0);
                           });
    EXPECT_TRUE(r.netEquity.back() > 1.0);
    EXPECT_THROWS(bt::run(ctx, factors::momentum(20, 0), config,
                         [](const AlphaContext&, const std::vector<double>&, int) {
                             return std::vector<double>(1, 0.0);
                         }));
}

TEST(decayIsFlatForPersistentSignalsAndFiniteForFadingOnes) {
    AlphaContext ctx = panel();
    rep::Decay persistent = rep::decayProfile(ctx, factors::momentum(20, 0), 30, {1, 2, 5, 10});
    for (double ic : persistent.meanIcs) {
        EXPECT_NEAR(ic, 1.0, 1e-9);
    }
    EXPECT_TRUE(std::isinf(persistent.halfLifeBars));
    EXPECT_TRUE(persistent.format().find("half-life") != std::string::npos);
    EXPECT_THROWS(rep::decayProfile(ctx, factors::momentum(20, 0), 30, {5, 2}));
}

TEST(attributionRecoversKnownBetasExactly) {
    int n = 100;
    quantfinlib::JavaRandom rng(3);
    std::vector<double> f1(static_cast<std::size_t>(n));
    std::vector<double> f2(static_cast<std::size_t>(n));
    std::vector<double> y(static_cast<std::size_t>(n));
    for (int t = 0; t < n; t++) {
        f1[static_cast<std::size_t>(t)] = (rng.nextDouble() - 0.5) * 0.02;
        f2[static_cast<std::size_t>(t)] = (rng.nextDouble() - 0.5) * 0.02;
        y[static_cast<std::size_t>(t)] =
                0.0002 + 1.5 * f1[static_cast<std::size_t>(t)] - 0.5 * f2[static_cast<std::size_t>(t)];
    }
    rep::Attribution a = rep::attribute(y, {f1, f2}, {"MOM", "VAL"});
    EXPECT_NEAR(a.alphaPerBar, 0.0002, 1e-12);
    EXPECT_NEAR(a.betas[0], 1.5, 1e-9);
    EXPECT_NEAR(a.betas[1], -0.5, 1e-9);
    EXPECT_NEAR(a.rSquared, 1.0, 1e-9);
    EXPECT_TRUE(a.format().find("MOM") != std::string::npos);
    EXPECT_THROWS(rep::attribute(y, {f1}, {"A", "B"}));
}

TEST(curvesAndRollingMetrics) {
    std::vector<double> equity = {1.0, 1.1, 1.05, 1.2, 1.1, 1.3};
    std::vector<double> dd = rep::drawdownCurve(equity);
    EXPECT_NEAR(dd[0], 0.0, 0.0);
    EXPECT_NEAR(dd[2], 1.05 / 1.1 - 1, 1e-12);
    EXPECT_NEAR(dd[5], 0.0, 1e-12);
    std::vector<double> returns = rep::returnsOf(equity);
    EXPECT_TRUE(returns.size() == 5);
    EXPECT_NEAR(returns[0], 0.1, 1e-12);

    std::vector<double> rolling = rep::rollingSharpe(returns, 3, 252);
    EXPECT_TRUE(std::isnan(rolling[0]) && std::isnan(rolling[1]));
    EXPECT_TRUE(!std::isnan(rolling[2]));
    EXPECT_TRUE(rep::summarize(equity, 252).sharpeRatio > 0);
    EXPECT_THROWS(rep::rollingSharpe(returns, 1, 252));
    EXPECT_THROWS(rep::returnsOf(std::vector<double>{1.0}));
}

QTEST_MAIN()
