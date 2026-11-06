// Phase 2B engine tests: PortfolioBacktester (classic run) and
// CrossSectionalMomentum. Rebalance pins are hand-derived: 50/50 into
// two symbols at 10 costs 1000000 of turnover; commission 0.001 charges
// 1000; a flat(20) cost model charges double that, proving the
// TradeCostModel seam supersedes the flat rate.
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/portfolio/cross_sectional_momentum.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"
#include "quantfinlib/backtest/trade_cost_model.hpp"
#include "quantfinlib/data/bar_series.hpp"

using namespace quantfinlib;
namespace pbt = quantfinlib::portfolio_backtester;

namespace {

/** Constant target weights on every rebalance. */
class FixedWeights final : public PortfolioStrategy {
public:
    explicit FixedWeights(std::map<std::string, double> weights)
            : weights_(std::move(weights)) {}
    std::string name() const override { return "FIXED"; }
    void init(const std::map<std::string, BarSeries>&) override {}
    std::map<std::string, double> targetWeights(int) override { return weights_; }

private:
    std::map<std::string, double> weights_;
};

std::map<std::string, BarSeries> twoSymbols() {
    return {{"A", BarSeries::of("A", {10, 11})}, {"B", BarSeries::of("B", {10, 9})}};
}

}  // namespace

// ------------------------------------------------------ PortfolioBacktester

TEST(rebalanceTowardTargetWeightsZeroCommission) {
    FixedWeights strategy({{"A", 0.5}, {"B", 0.5}});
    pbt::Config config = pbt::Config{1000000, 0.0, 1, 252, nullptr}.withIdealisedExecution();
    pbt::Result result = pbt::run(strategy, twoSymbols(), config);

    // Bar 0: 50000 shares each at 10. Bar 1: A=11, B=9 -> PV still 1M;
    // rebalance sells A down to 500000/11 and buys B up to 500000/9.
    EXPECT_NEAR(result.equityCurve[0], 1000000.0, 1e-6);
    EXPECT_NEAR(result.equityCurve[1], 1000000.0, 1e-6);
    EXPECT_NEAR(result.finalPositions.at("A"), 500000.0 / 11, 1e-6);
    EXPECT_NEAR(result.finalPositions.at("B"), 500000.0 / 9, 1e-6);
    // Turnover: 1M on bar 0, then |delta| * close = 50000 + 50000 on bar 1.
    EXPECT_NEAR(result.totalTurnoverNotional, 1000000.0 + 100000.0, 1e-6);
    EXPECT_NEAR(result.totalCosts, 0.0, 1e-12);
}

TEST(commissionChargedOnTradedNotional) {
    FixedWeights strategy({{"A", 0.5}, {"B", 0.5}});
    pbt::Config config = pbt::Config{1000000, 0.001, 1, 252, nullptr}.withIdealisedExecution();
    pbt::Result result = pbt::run(strategy, twoSymbols(), config);
    // Bar 0 trades 1M of notional -> 1000 of fees.
    EXPECT_NEAR(result.equityCurve[0], 999000.0, 1e-6);
    EXPECT_TRUE(result.totalCosts > 1000.0 - 1e-9);
}

TEST(costModelSupersedesFlatCommission) {
    // flat(20 bps) = 0.002 vs commissionRate 0.001: fees double, proving
    // the TradeCostModel seam supersedes the legacy flat rate.
    FixedWeights strategy({{"A", 0.5}, {"B", 0.5}});
    pbt::Config config = pbt::Config{1000000, 0.001, 1, 252, nullptr}.withIdealisedExecution().withCostModel(
            trade_cost_model::flat(20));
    pbt::Result result = pbt::run(strategy, twoSymbols(), config);
    EXPECT_NEAR(result.equityCurve[0], 998000.0, 1e-6);
}

TEST(shortWeightsAndLeverageAreAllowed) {
    // Long/short dollar-neutral book: +0.5 A, -0.5 B. A rallies, B sinks:
    // both legs win. PV0 = 1M; bar 1 PV = cash(1M) + 50000*11 - 50000*9
    // = 1.1M... wait: long 50000 A (500k), short 50000 B (-500k), cash 1M.
    // Bar 1: 1M + 550000 - 450000 = 1.1M.
    FixedWeights strategy({{"A", 0.5}, {"B", -0.5}});
    pbt::Config config = pbt::Config{1000000, 0.0, 1, 252, nullptr}.withIdealisedExecution();
    pbt::Result result = pbt::run(strategy, twoSymbols(), config);
    EXPECT_NEAR(result.equityCurve[0], 1000000.0, 1e-6);
    EXPECT_NEAR(result.equityCurve[1], 1100000.0, 1e-6);
}

TEST(rebalanceCadenceSkipsBars) {
    std::map<std::string, BarSeries> data = {
            {"A", BarSeries::of("A", {10, 12, 15, 20})}};
    FixedWeights strategy({{"A", 1.0}});
    pbt::Config config = pbt::Config{1000000, 0.0, 3, 252, nullptr}.withIdealisedExecution();
    pbt::Result result = pbt::run(strategy, data, config);
    // Trades on bars 0 and 3 only: bar-0 buy of 100000 shares (1M), then
    // bar-3 the position is already worth PV -> delta 0. Turnover = 1M.
    EXPECT_NEAR(result.totalTurnoverNotional, 1000000.0, 1e-6);
    EXPECT_NEAR(result.equityCurve[3], 2000000.0, 1e-6);
}

TEST(portfolioGates) {
    FixedWeights strategy({{"A", 1.0}});
    EXPECT_THROWS(pbt::run(strategy, {}, pbt::Config::defaults()));
    std::map<std::string, BarSeries> misaligned = {
            {"A", BarSeries::of("A", {10, 11})}, {"B", BarSeries::of("B", {10, 11, 12})}};
    EXPECT_THROWS(pbt::run(strategy, misaligned, pbt::Config::defaults()));
}

// --------------------------------------------------- CrossSectionalMomentum

TEST(momentumRanksWinnersLongLosersShort) {
    // lookback 2, skip 0: momentum(i) = close(i)/close(i-2) - 1.
    // A quadruples, B flat, C halves -> +0.5 A, -0.5 C, B flat.
    std::map<std::string, BarSeries> data = {
            {"A", BarSeries::of("A", {1, 2, 4})},
            {"B", BarSeries::of("B", {5, 5, 5})},
            {"C", BarSeries::of("C", {8, 4, 2})}};
    CrossSectionalMomentum strategy(CrossSectionalMomentum::Config(2, 0, 1, 0.5));
    strategy.init(data);

    EXPECT_TRUE(strategy.targetWeights(0).empty());
    EXPECT_TRUE(strategy.targetWeights(1).empty());  // index < lookback
    std::map<std::string, double> w = strategy.targetWeights(2);
    EXPECT_TRUE(w.size() == 2);
    EXPECT_NEAR(w.at("A"), 0.5, 1e-12);
    EXPECT_NEAR(w.at("C"), -0.5, 1e-12);
    EXPECT_TRUE(w.find("B") == w.end());
}

TEST(momentumSideShrinksWhenTooFewNames) {
    // perSide 5 but only 3 names: side shrinks to 3/2 = 1 (no overlap).
    std::map<std::string, BarSeries> data = {
            {"A", BarSeries::of("A", {1, 2, 4})},
            {"B", BarSeries::of("B", {5, 5, 5})},
            {"C", BarSeries::of("C", {8, 4, 2})}};
    CrossSectionalMomentum strategy(CrossSectionalMomentum::Config(2, 0, 5, 0.5));
    strategy.init(data);
    std::map<std::string, double> w = strategy.targetWeights(2);
    EXPECT_TRUE(w.size() == 2);
    EXPECT_NEAR(w.at("A"), 0.5, 1e-12);
    EXPECT_NEAR(w.at("C"), -0.5, 1e-12);
}

TEST(momentumConfigGatesAndPresets) {
    EXPECT_THROWS(CrossSectionalMomentum::Config(0, 0, 1, 0.5));
    EXPECT_THROWS(CrossSectionalMomentum::Config(10, 10, 1, 0.5));  // skip >= lookback
    EXPECT_THROWS(CrossSectionalMomentum::Config(10, -1, 1, 0.5));
    EXPECT_THROWS(CrossSectionalMomentum::Config(10, 1, 0, 0.5));
    EXPECT_THROWS(CrossSectionalMomentum::Config(10, 1, 1, 0.0));

    CrossSectionalMomentum::Config preset = CrossSectionalMomentum::Config::twelveMinusOne(3);
    EXPECT_TRUE(preset.lookbackBars == 252);
    EXPECT_TRUE(preset.skipBars == 21);
    EXPECT_TRUE(preset.perSide == 3);
    EXPECT_NEAR(preset.grossPerSide, 0.5, 1e-12);

    CrossSectionalMomentum strategy(CrossSectionalMomentum::Config(2, 0, 1, 0.5));
    EXPECT_TRUE(strategy.name() == "XS_MOMENTUM(2-0, 1/side)");
}

TEST(momentumBookRunsThroughThePortfolioEngine) {
    // End-to-end: a dollar-neutral momentum book long the riser, short
    // the faller, on a tape where both legs keep paying.
    std::vector<double> up(10), flat(10, 50.0), down(10);
    for (int i = 0; i < 10; i++) {
        up[static_cast<std::size_t>(i)] = 100 * std::pow(1.05, i);
        down[static_cast<std::size_t>(i)] = 100 * std::pow(0.95, i);
    }
    std::map<std::string, BarSeries> data = {{"UP", BarSeries::of("UP", up)},
                                             {"FLAT", BarSeries::of("FLAT", flat)},
                                             {"DOWN", BarSeries::of("DOWN", down)}};
    CrossSectionalMomentum strategy(CrossSectionalMomentum::Config(3, 1, 1, 0.5));
    pbt::Config config = pbt::Config{1000000, 0.0, 1, 252, nullptr}.withIdealisedExecution();
    pbt::Result result = pbt::run(strategy, data, config);

    // Long UP, short DOWN from bar 3: both legs profit every bar.
    EXPECT_TRUE(result.equityCurve.back() > 1000000.0);
    EXPECT_TRUE(result.finalPositions.at("UP") > 0);
    EXPECT_TRUE(result.finalPositions.at("DOWN") < 0);
}

QTEST_MAIN()
