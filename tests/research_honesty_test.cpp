// A backtest must not flatter: fill timing, default costs, volume caps,
// as-of fundamentals, weight drift, carry and delistings in the three bar
// engines. Expected values are worked by hand from the bars below.
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/alpha/alpha_backtester.hpp"
#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/factors.hpp"
#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/execution_aware_backtester.hpp"
#include "quantfinlib/backtest/instant_execution.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"
#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/microstructure/market_impact_model.hpp"

using namespace quantfinlib;
namespace pbt = portfolio_backtester;
namespace abt = alpha_backtester;

namespace {

struct TestBar {
    double open, high, low, close, volume;
};

BarSeries seriesOf(const std::string& symbol, const std::vector<TestBar>& bars) {
    auto b = BarSeries::builder(symbol);
    std::int64_t t = 0;
    for (const TestBar& x : bars) {
        b.add(t++, x.open, x.high, x.low, x.close, x.volume);
    }
    return b.build();
}

/** Plays back a fixed signal list. */
class Script final : public TradingStrategy {
public:
    explicit Script(std::map<int, Signal> signals) : signals_(std::move(signals)) {}
    std::string name() const override { return "script"; }
    void init(const BarSeries&) override {}
    Signal onBar(int i) override {
        auto it = signals_.find(i);
        return it == signals_.end() ? Signal::HOLD : it->second;
    }

private:
    std::map<int, Signal> signals_;
};

class FixedWeights final : public PortfolioStrategy {
public:
    explicit FixedWeights(std::map<std::string, double> w) : w_(std::move(w)) {}
    std::string name() const override { return "fixed"; }
    void init(const std::map<std::string, BarSeries>&) override {}
    std::map<std::string, double> targetWeights(int) override { return w_; }

private:
    std::map<std::string, double> w_;
};

// Close and next open differ on every bar, so a wrong fill price shows.
const std::vector<TestBar> kBars = {{100, 101, 99, 100, 1e9},
                                {100, 101, 99, 100, 1e9},
                                {110, 121, 109, 120, 1e9},
                                {130, 141, 129, 140, 1e9},
                                {150, 161, 149, 160, 1e9}};

BacktestConfig noCosts() {
    BacktestConfig c = BacktestConfig::defaults();
    c.commissionRate = 0;
    c.slippageRate = 0;
    return c;
}

}  // namespace

// ------------------------------------------------------------ defaults

TEST(defaultsAreNextOpenWithCostsAndAVolumeCap) {
    BacktestConfig b;
    EXPECT_TRUE(b.fillTiming == FillTiming::NEXT_BAR_OPEN);
    EXPECT_TRUE(b.commissionRate > 0 && b.slippageRate > 0 && b.maxParticipationRate > 0);
    EXPECT_TRUE(BacktestConfig::defaults().slippageRate > 0);
    pbt::Config p;
    EXPECT_TRUE(p.fillTiming == FillTiming::NEXT_BAR_OPEN);
    EXPECT_TRUE(p.commissionRate > 0 && p.maxParticipationRate > 0);
    EXPECT_TRUE(p.borrowRateAnnual > 0 && p.financingRateAnnual > 0);
    abt::Config a;
    EXPECT_TRUE(a.fillTiming == FillTiming::NEXT_BAR_OPEN);
    EXPECT_TRUE(a.commissionBps > 0 && a.maxParticipationRate > 0);
    EXPECT_TRUE(a.borrowBpsAnnual > 0 && a.financingBpsAnnual > 0);
}

// ---------------------------------------------------------- Backtester

TEST(backtesterFillsSignalAtNextOpenByDefault) {
    Script s({{1, Signal::BUY}, {3, Signal::SELL}});
    BacktestResult r = backtester::run(s, seriesOf("X", kBars), noCosts());
    // Signal on bar 1's close (100) buys at bar 2's open (110); signal on
    // bar 3's close sells at bar 4's open (150).
    EXPECT_TRUE(r.trades().size() == 1);
    EXPECT_NEAR(r.trades()[0].entryPrice, 110, 1e-12);
    EXPECT_NEAR(r.trades()[0].exitPrice, 150, 1e-12);
    EXPECT_TRUE(r.trades()[0].entryIndex == 2 && r.trades()[0].exitIndex == 4);
    EXPECT_NEAR(r.equityCurve().back(), 100000.0 * 150 / 110, 1e-6);
}

TEST(backtesterSameBarFillIsAnExplicitOptIn) {
    Script s({{1, Signal::BUY}, {3, Signal::SELL}});
    BacktestResult r = backtester::run(s, seriesOf("X", kBars), noCosts().withSameBarCloseFill());
    EXPECT_NEAR(r.trades()[0].entryPrice, 100, 1e-12);
    EXPECT_NEAR(r.trades()[0].exitPrice, 140, 1e-12);
}

TEST(backtesterDropsASignalWithNoNextBar) {
    Script s({{4, Signal::BUY}});
    BacktestResult r = backtester::run(s, seriesOf("X", kBars), noCosts());
    EXPECT_TRUE(r.trades().empty());
    EXPECT_NEAR(r.equityCurve().back(), 100000, 1e-9);
}

TEST(backtesterStopCoversTheRestOfANextOpenEntryBar) {
    // Enters at bar 1's open (100); bar 1's low (80) breaches a 10% stop.
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1e9}, {100, 100, 80, 90, 1e9}, {90, 90, 90, 90, 1e9}};
    Script s({{0, Signal::BUY}});
    BacktestResult r = backtester::run(s, seriesOf("X", bars), noCosts().withStopLoss(0.10));
    EXPECT_TRUE(r.trades().size() == 1);
    EXPECT_TRUE(std::string(r.trades()[0].exitReason) == Trade::REASON_STOP_LOSS);
    EXPECT_NEAR(r.trades()[0].exitPrice, 90, 1e-12);
    EXPECT_TRUE(r.trades()[0].exitIndex == 1);
}

TEST(backtesterStopPaysSlippage) {
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1e9}, {100, 100, 80, 90, 1e9}, {90, 90, 90, 90, 1e9}};
    Script s({{0, Signal::BUY}});
    BacktestConfig c = noCosts().withStopLoss(0.10).withSlippage(0.01);
    BacktestResult r = backtester::run(s, seriesOf("X", bars), c);
    // Entry 101; stop at 90.9; sold at 90.9 less 1%.
    EXPECT_NEAR(r.trades()[0].exitPrice, 101 * 0.9 * 0.99, 1e-9);
}

TEST(backtesterCapsEntryAtParticipationAndKeepsTheCash) {
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1000}, {100, 100, 100, 100, 1000}, {120, 120, 120, 120, 1000}};
    Script s({{0, Signal::BUY}});
    BacktestResult r = backtester::run(s, seriesOf("X", bars), noCosts());
    // 10% of 1,000 = 100 shares at 100 = 10,000 spent of 100,000.
    EXPECT_NEAR(r.trades()[0].quantity, 100, 1e-9);
    EXPECT_NEAR(r.equityCurve().back(), 90000 + 100 * 120, 1e-6);
}

TEST(backtesterCappedExitCarriesItsRemainder) {
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1e6},  // buy decided
                             {100, 100, 100, 100, 1e6},  // bought 1,000; sell decided
                             {100, 100, 100, 100, 4000}, // 400 sold
                             {100, 100, 100, 100, 4000}, // 400 sold
                             {100, 100, 100, 100, 4000}, // last 200 sold
                             {100, 100, 100, 100, 4000}};
    Script s({{0, Signal::BUY}, {1, Signal::SELL}});
    BacktestResult r = backtester::run(s, seriesOf("X", bars), noCosts());
    EXPECT_TRUE(r.trades().size() == 3);
    EXPECT_NEAR(r.trades()[0].quantity, 400, 1e-9);
    EXPECT_TRUE(r.trades()[1].exitIndex == 3);
    EXPECT_NEAR(r.trades()[2].quantity, 200, 1e-9);
    EXPECT_NEAR(r.equityCurve().back(), 100000, 1e-6);
}

TEST(zeroVolumeBarDoesNotTradeButVolumelessSeriesDoes) {
    std::vector<TestBar> halted = {{100, 100, 100, 100, 1e9}, {100, 100, 100, 100, 0}, {100, 100, 100, 100, 1e9}};
    Script a({{0, Signal::BUY}});
    EXPECT_TRUE(backtester::run(a, seriesOf("X", halted), noCosts()).trades().empty());
    std::vector<TestBar> noVolume = {{100, 100, 100, 100, 0}, {100, 100, 100, 100, 0}, {120, 120, 120, 120, 0}};
    Script b({{0, Signal::BUY}});
    BacktestResult r = backtester::run(b, seriesOf("X", noVolume), noCosts());
    EXPECT_NEAR(r.equityCurve().back(), 120000, 1e-6);
}

TEST(badParticipationRateIsRefused) {
    Script s({});
    BarSeries x = seriesOf("X", kBars);
    EXPECT_THROWS(backtester::run(s, x, noCosts().withMaxParticipation(std::nan(""))));
    EXPECT_THROWS(backtester::run(s, x, noCosts().withMaxParticipation(1.5)));
}

// ---------------------------------------- ExecutionAwareBacktester

TEST(executionAwareFillsAtNextOpenByDefault) {
    Script s({{1, Signal::BUY}, {3, Signal::SELL}});
    BarSeries x = seriesOf("X", kBars);
    BacktestConfig c = noCosts();
    InstantExecution model = InstantExecution::from(c);
    ExecutionAwareResult r = ExecutionAwareBacktester::run(s, x, c, model);
    EXPECT_NEAR(r.parentOrders()[0].fills[0].price, 110, 1e-12);
    EXPECT_TRUE(r.parentOrders()[0].fillBarIndices[0] == 2);
    EXPECT_NEAR(r.parentOrders()[0].arrivalPrice, 100, 1e-12);  // TCA still measures from the signal
    EXPECT_NEAR(r.parentOrders()[1].fills[0].price, 150, 1e-12);
}

TEST(executionAwareStopDoesNotFillBeforeItTriggers) {
    // TestBar 2 opens at 100 (above the stop) and trades down through it. A
    // fill at bar 2's open would beat the stop with a price from before it.
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1e9}, {100, 100, 100, 100, 1e9},
                             {100, 100, 70, 75, 1e9},   {72, 72, 72, 72, 1e9},
                             {72, 72, 72, 72, 1e9}};
    Script s({{0, Signal::BUY}});
    BacktestConfig c = noCosts().withStopLoss(0.10);
    InstantExecution model = InstantExecution::from(c);
    ExecutionAwareResult r = ExecutionAwareBacktester::run(s, seriesOf("X", bars), c, model);
    EXPECT_NEAR(r.parentOrders()[1].fills[0].price, 72, 1e-12);
    EXPECT_TRUE(r.parentOrders()[1].fillBarIndices[0] == 3);
}

TEST(executionAwareCapsChildrenAtParticipation) {
    std::vector<TestBar> bars(6, TestBar{100, 100, 100, 100, 1000});
    Script s({{0, Signal::BUY}});
    BacktestConfig c = noCosts();
    InstantExecution model = InstantExecution::from(c);
    ExecutionAwareResult r = ExecutionAwareBacktester::run(s, seriesOf("X", bars), c, model);
    for (const Execution& f : r.parentOrders()[0].fills) {
        EXPECT_TRUE(f.quantity <= 100);
    }
    EXPECT_TRUE(r.parentOrders()[0].fills.size() == 5);  // one per bar, bars 1-5
}

// ------------------------------------------------- PortfolioBacktester

namespace {
std::map<std::string, BarSeries> onePanel(const std::vector<TestBar>& bars) {
    return {{"X", seriesOf("X", bars)}};
}
pbt::Config freePortfolio() {
    pbt::Config c = pbt::Config::defaults();
    c.commissionRate = 0;
    c.borrowRateAnnual = 0;
    c.financingRateAnnual = 0;
    return c;
}
}  // namespace

TEST(portfolioTradesAtNextOpen) {
    FixedWeights s({{"X", 1.0}});
    pbt::Config c = freePortfolio();
    c.rebalanceEveryBars = 100;  // decide on bar 0 only
    std::vector<TestBar> bars = {{100, 100, 100, 100, 1e9}, {125, 125, 125, 150, 1e9}};
    pbt::Result r = pbt::run(s, onePanel(bars), c);
    // 1,000,000 buys at 125, not at bar 0's close: 8,000 shares at close 150.
    EXPECT_NEAR(r.finalPositions.at("X"), 8000, 1e-9);
    EXPECT_NEAR(r.equityCurve[0], 1000000, 1e-6);
    EXPECT_NEAR(r.equityCurve[1], 1200000, 1e-6);
}

TEST(portfolioCapsTradesAtParticipation) {
    FixedWeights s({{"X", 1.0}});
    pbt::Config c = freePortfolio();
    std::vector<TestBar> bars(3, TestBar{100, 100, 100, 100, 1000});
    pbt::Result r = pbt::run(s, onePanel(bars), c);
    EXPECT_NEAR(r.finalPositions.at("X"), 200, 1e-9);  // bars 1 and 2, 100 each
}

TEST(portfolioShortPaysBorrowAndDebitPaysFinancing) {
    pbt::Config c = freePortfolio().withIdealisedExecution();
    c.borrowRateAnnual = 0.0252;
    c.periodsPerYear = 252;
    std::vector<TestBar> flat(2, TestBar{100, 100, 100, 100, 0});
    FixedWeights shortBook({{"X", -1.0}});
    pbt::Result s = pbt::run(shortBook, onePanel(flat), c);
    // 1,000,000 short for two bars at 1 bp a bar.
    EXPECT_NEAR(s.totalBorrowCost, 200, 0.05);
    EXPECT_NEAR(s.equityCurve.back(), 1000000 - s.totalBorrowCost, 1e-6);

    pbt::Config f = freePortfolio().withIdealisedExecution();
    f.financingRateAnnual = 0.0252;
    FixedWeights levered({{"X", 2.0}});
    pbt::Result l = pbt::run(levered, onePanel(flat), f);
    EXPECT_NEAR(l.totalFinancingCost, 200, 0.05);  // 1,000,000 debit, two bars
    EXPECT_TRUE(l.totalBorrowCost == 0);
}

TEST(portfolioDelistingPaysTheDelistingReturnNotTheLastPrice) {
    std::vector<TestBar> a = {{100, 100, 100, 100, 0}, {100, 100, 100, 100, 0}, {100, 100, 100, 100, 0}, {100, 100, 100, 100, 0}};
    std::map<std::string, BarSeries> data = {{"X", seriesOf("X", a)}, {"Y", seriesOf("Y", a)}};
    FixedWeights s({{"X", 0.5}, {"Y", 0.5}});
    pbt::Config c = freePortfolio().withIdealisedExecution();
    c.delistings.push_back(Delisting{"X", 2, -0.30});
    pbt::Result r = pbt::run(s, data, c);
    // 500,000 of X becomes 350,000 of cash and is not bought back.
    EXPECT_NEAR(r.equityCurve[1], 1000000, 1e-6);
    EXPECT_NEAR(r.equityCurve[2], 850000, 1e-6);
    EXPECT_TRUE(r.finalPositions.count("X") == 0);
    pbt::Config bad = c;
    bad.delistings[0].delistingReturn = std::nan("");
    EXPECT_THROWS(pbt::run(s, data, bad));
}

// ------------------------------------------------------ AlphaBacktester

namespace {

AlphaFactor flatFactor() {
    return AlphaFactor("flat", [](const AlphaContext& ctx, int) {
        return std::vector<double>(static_cast<std::size_t>(ctx.symbolCount()), 1.0);
    });
}

abt::WeightBuilder fixedBuilder(std::vector<double> w) {
    return [w](const AlphaContext&, const std::vector<double>&, int) { return w; };
}

abt::Config alphaConfig(int rebalance) {
    abt::Config c;
    c.startIndex = 0;
    c.rebalanceEveryBars = rebalance;
    c.commissionBps = 10;
    c.halfSpreadBps = 0;
    c.slippageBps = 0;
    c.capital = 0;
    c.borrowBpsAnnual = 0;
    c.financingBpsAnnual = 0;
    return c;
}

std::unordered_map<std::string, BarSeries> closesPanel(const std::vector<double>& a,
                                                       const std::vector<double>& b,
                                                       double volume = 1e9) {
    std::unordered_map<std::string, BarSeries> data;
    for (int k = 0; k < 2; k++) {
        const auto& closes = k == 0 ? a : b;
        std::vector<TestBar> bars;
        for (double c : closes) {
            bars.push_back(TestBar{c, c, c, c, volume});
        }
        std::string name = k == 0 ? "A" : "B";
        data.emplace(name, seriesOf(name, bars));
    }
    return data;
}

}  // namespace

TEST(alphaWeightsDriftSoARebalancePaysToReturnToTarget) {
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 200, 200, 200}, {100, 100, 100, 100}));
    abt::Config c = alphaConfig(2).withIdealisedExecution();
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));
    // TestBar 0 trades 1.0 of equity. A doubles: weights 2/3, 1/3 by bar 2,
    // which takes 1/6 + 1/6 to reset. 10 bps on each.
    EXPECT_NEAR(r.commissionDrag, (1.0 + 1.0 / 3.0) * 10 / 1e4, 1e-12);
    EXPECT_NEAR(r.grossEquity[1], 1.5, 1e-12);
}

TEST(alphaHoldingPeriodCompoundsDriftedWeights) {
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 200, 400}, {100, 100, 100}));
    abt::Config c = alphaConfig(10).withIdealisedExecution();
    c.commissionBps = 0;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));
    // Buy and hold: 0.5*4 + 0.5*1. Fixed weights would give 1.5 * 1.5.
    EXPECT_NEAR(r.grossEquity.back(), 2.5, 1e-12);
}

TEST(alphaTradesAtNextOpenByDefault) {
    std::unordered_map<std::string, BarSeries> data;
    data.emplace("A", seriesOf("A", {{100, 100, 100, 100, 1e9}, {200, 200, 200, 220, 1e9}}));
    AlphaContext ctx = AlphaContext::of(data);
    abt::Config c = alphaConfig(1);
    c.commissionBps = 0;
    c.maxParticipationRate = 0;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({1.0}));
    // The overnight jump is missed: bought at 200, marked at 220.
    EXPECT_NEAR(r.grossEquity[1], 1.1, 1e-12);
}

TEST(alphaImpactUsesCurrentCapital) {
    // 40 bars of noisy prices so ADV and vol estimate; A trends up hard.
    std::vector<double> a, b;
    for (int i = 0; i < 40; i++) {
        a.push_back(100 * std::pow(1.05, i) * (i % 2 == 0 ? 1.001 : 0.999));
        b.push_back(100 * (i % 2 == 0 ? 1.002 : 0.998));
    }
    auto data = closesPanel(a, b, 1e6);
    AlphaContext ctx = AlphaContext::of(data);
    abt::Config c = alphaConfig(10).withIdealisedExecution();
    c.startIndex = 20;
    c.capital = 1e7;
    c.commissionBps = 0;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));

    // Rebuild the expected drag rebalance by rebalance: 20 and 30.
    double impact = 0;
    double wA = 0, wB = 0, equity = 1;
    for (int t : {20, 30}) {
        equity = r.netEquity[static_cast<std::size_t>(t - 20)];
        if (t == 30) {
            double growthA = a[30] / a[20], growthB = b[30] / b[20];
            double total = 0.5 * growthA + 0.5 * growthB;
            wA = 0.5 * growthA / total;
            wB = 0.5 * growthB / total;
        }
        double weights[2] = {wA, wB};
        for (int i = 0; i < 2; i++) {
            const BarSeries& s = ctx.series(i);
            double traded = std::abs(0.5 - weights[i]);
            auto model = MarketImpactModel::estimate(s, t, c.impactWindow);
            impact += traded * model->squareRootImpactBps(traded * c.capital * equity / s.close(t)) / 1e4;
        }
    }
    EXPECT_NEAR(r.impactDrag, impact, 1e-12);
    EXPECT_TRUE(equity > 1.2);  // far enough from 1 that the starting book would fail
}

TEST(alphaVolumeCapLeavesTheTargetUnreached) {
    std::vector<double> flat(6, 100.0);
    AlphaContext ctx = AlphaContext::of(closesPanel(flat, flat, 1000));
    abt::Config c = alphaConfig(1);
    c.commissionBps = 0;
    c.capital = 1e6;  // 0.5 weight = 5,000 shares; the cap allows 100 a bar
    c.impactWindow = 2;
    c.startIndex = 2;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));
    // Each rebalance moves 100 shares = 0.01 of the book in each of two names.
    EXPECT_NEAR(r.meanTurnover, 0.01, 1e-9);
}

TEST(alphaShortsPayBorrowAndLeveragePaysFinancing) {
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 100, 100}, {100, 100, 100}));
    abt::Config c = alphaConfig(1).withIdealisedExecution();
    c.commissionBps = 0;
    c.borrowBpsAnnual = 252;
    c.financingBpsAnnual = 2520;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({2.0, -0.5}));
    // Two bars. Short 0.5 at 1 bp a bar; net long 1.5, so 0.5 financed at 10 bps a bar.
    EXPECT_NEAR(r.borrowDrag, 2 * 0.5 * 1e-4, 1e-12);
    EXPECT_NEAR(r.financingDrag, 2 * 0.5 * 1e-3, 1e-12);
    EXPECT_TRUE(r.netEquity.back() < r.grossEquity.back());
}

TEST(alphaDelistingPaysItsReturnAndLeavesTheBook) {
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 100, 100, 100}, {100, 100, 100, 100}));
    abt::Config c = alphaConfig(1).withIdealisedExecution();
    c.commissionBps = 0;
    c.delistings.push_back(Delisting{"A", 2, -0.5});
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));
    EXPECT_NEAR(r.grossEquity[1], 1.0, 1e-12);
    EXPECT_NEAR(r.grossEquity[2], 0.75, 1e-12);
    EXPECT_NEAR(r.grossEquity[3], 0.75, 1e-12);  // no position left to bounce
    abt::Config bad = c;
    bad.delistings[0].symbol = "NOPE";
    EXPECT_THROWS(abt::run(ctx, flatFactor(), bad, fixedBuilder({0.5, 0.5})));
}

TEST(alphaReadsDelistingsFromTheUniverse) {
    auto universe = std::make_shared<PointInTimeUniverse>();
    universe->addMembership("A", 0).addMembership("B", 0);
    universe->recordDelisting("A", 2, -1.0);
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 100, 100, 100}, {100, 100, 100, 100}))
                               .withUniverse(universe);
    abt::Config c = alphaConfig(1).withIdealisedExecution();
    c.commissionBps = 0;
    abt::Result r = abt::run(ctx, flatFactor(), c, fixedBuilder({0.5, 0.5}));
    EXPECT_NEAR(r.grossEquity[2], 0.5, 1e-12);
}

TEST(alphaRefusesNonFiniteWeights) {
    AlphaContext ctx = AlphaContext::of(closesPanel({100, 100}, {100, 100}));
    abt::Config c = alphaConfig(1);
    EXPECT_THROWS(abt::run(ctx, flatFactor(), c, fixedBuilder({std::nan(""), 0.5})));
}

// --------------------------------------------------- as-of fundamentals

TEST(fundamentalsAreVisibleOnlyFromPublication) {
    Fundamentals early;
    early.roe = 0.10;
    early.debtToEquity = 0;
    Fundamentals later;
    later.roe = 0.30;
    later.debtToEquity = 0;
    std::unordered_map<std::string, std::vector<DatedFundamentals>> dated;
    dated["A"] = {DatedFundamentals{3, later}, DatedFundamentals{1, early}};  // order must not matter
    AlphaContext ctx = AlphaContext::of(closesPanel({1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}), dated);
    AlphaFactor quality = factors::quality();
    EXPECT_TRUE(std::isnan(quality.scores(ctx, 0)[0]));     // nothing public yet
    EXPECT_NEAR(quality.scores(ctx, 1)[0], 0.10, 1e-12);    // public on its own date
    EXPECT_NEAR(quality.scores(ctx, 2)[0], 0.10, 1e-12);
    EXPECT_NEAR(quality.scores(ctx, 3)[0], 0.30, 1e-12);
    EXPECT_TRUE(std::isnan(quality.scores(ctx, 3)[1]));     // no record for B
}

TEST(undatedSnapshotMustBeAskedForByName) {
    Fundamentals f;
    f.roe = 0.20;
    f.debtToEquity = 0;
    AlphaContext ctx = AlphaContext::ofUndatedSnapshot(closesPanel({1, 1}, {1, 1}), {{"A", f}});
    EXPECT_NEAR(factors::quality().scores(ctx, 0)[0], 0.20, 1e-12);
}

QTEST_MAIN()
