// Phase 2B engine tests: ParameterGrid, GridSearchOptimizer (planted
// optimum + deflated-Sharpe haircut) and WalkForwardAnalyzer (fold
// arithmetic, train-only optimization / no look-ahead, warm folds, and
// the efficiency-NaN guard).
#include <cmath>
#include <memory>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/strategies/sma_cross_strategy.hpp"
#include "quantfinlib/backtest/validation/grid_search_optimizer.hpp"
#include "quantfinlib/backtest/validation/parameter_grid.hpp"
#include "quantfinlib/backtest/validation/walk_forward_analyzer.hpp"
#include "quantfinlib/data/bar_series.hpp"

using namespace quantfinlib;
namespace gso = quantfinlib::grid_search_optimizer;
namespace wfa = quantfinlib::walk_forward_analyzer;

namespace {

BacktestConfig zeroCost() {
    return BacktestConfig{100000, 0.0, 0.0, 0.0, 0.0, 252}.withSameBarCloseFill().withoutVolumeCap();
}

/** Buys at the parameterized bar, holds to the end (force-closed). */
class EntryAtStrategy final : public TradingStrategy {
public:
    explicit EntryAtStrategy(int entryBar) : entryBar_(entryBar) {}
    std::string name() const override { return "ENTRY_AT"; }
    void init(const BarSeries&) override {}
    Signal onBar(int i) override { return i == entryBar_ ? Signal::BUY : Signal::HOLD; }

private:
    int entryBar_;
};

/** mode 1: BUY on every bar (enters ASAP, rides to force-close); mode 0: flat. */
class ModeStrategy final : public TradingStrategy {
public:
    explicit ModeStrategy(int mode) : mode_(mode) {}
    std::string name() const override { return "MODE"; }
    void init(const BarSeries&) override {}
    Signal onBar(int) override { return mode_ == 1 ? Signal::BUY : Signal::HOLD; }

private:
    int mode_;
};

double totalReturnObjective(const PerformanceMetrics& m) {
    return m.totalReturn;
}

}  // namespace

// ---------------------------------------------------------- ParameterGrid

TEST(parameterGridEnumeratesCartesianProductDeterministically) {
    ParameterGrid grid;
    grid.add("a", {1, 2}).add("b", {10, 20, 30});
    EXPECT_TRUE(grid.size() == 6);

    std::vector<Params> combos = grid.combinations();
    EXPECT_TRUE(combos.size() == 6);
    // First-added parameter varies slowest (Java LinkedHashMap order).
    EXPECT_NEAR(combos[0].at("a"), 1.0, 1e-12);
    EXPECT_NEAR(combos[0].at("b"), 10.0, 1e-12);
    EXPECT_NEAR(combos[2].at("a"), 1.0, 1e-12);
    EXPECT_NEAR(combos[2].at("b"), 30.0, 1e-12);
    EXPECT_NEAR(combos[3].at("a"), 2.0, 1e-12);
    EXPECT_NEAR(combos[3].at("b"), 10.0, 1e-12);
}

TEST(parameterGridEmptyAndGateBehavior) {
    ParameterGrid grid;
    EXPECT_TRUE(grid.size() == 0);
    EXPECT_TRUE(grid.combinations().empty());
    EXPECT_THROWS(grid.add("p", {}));

    // Re-adding a name replaces values, keeps position.
    grid.add("p", {1, 2, 3});
    grid.add("p", {5});
    EXPECT_TRUE(grid.size() == 1);
    EXPECT_NEAR(grid.combinations()[0].at("p"), 5.0, 1e-12);
}

// ----------------------------------------------------- GridSearchOptimizer

TEST(gridSearchFindsPlantedOptimum) {
    // V-shaped tape with the trough at bar 4: buying there maximizes the
    // hold-to-end return, and every other entry bar does strictly worse.
    std::vector<double> closes = {100, 90, 80, 70, 60, 90, 120, 150, 180, 200};
    BarSeries series = BarSeries::of("V", closes);

    ParameterGrid grid;
    grid.add("entry", {1, 2, 3, 4, 5, 6});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<EntryAtStrategy>(static_cast<int>(p.at("entry")));
    };

    std::vector<gso::Candidate> ranked =
            gso::search(grid, factory, series, zeroCost(), totalReturnObjective);
    EXPECT_TRUE(ranked.size() == 6);
    EXPECT_NEAR(ranked.front().params.at("entry"), 4.0, 1e-12);
    // Ranking is descending in the objective.
    for (std::size_t i = 1; i < ranked.size(); i++) {
        EXPECT_TRUE(ranked[i - 1].objective >= ranked[i].objective);
    }
    // best() is the head of the ranking; entry@4 returns 200/60 - 1.
    gso::Candidate best = gso::best(grid, factory, series, zeroCost(), totalReturnObjective);
    EXPECT_NEAR(best.objective, 200.0 / 60.0 - 1.0, 1e-9);
    EXPECT_THROWS(gso::best(ParameterGrid(), factory, series, zeroCost(),
                            totalReturnObjective));
}

TEST(deflatedSharpeOfWinnerGatesAndRange) {
    std::vector<double> closes = {100, 90, 80, 70, 60, 90, 120, 150, 180, 200};
    BarSeries series = BarSeries::of("V", closes);
    ParameterGrid grid;
    grid.add("entry", {1, 2, 3, 4, 5, 6});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<EntryAtStrategy>(static_cast<int>(p.at("entry")));
    };
    std::vector<gso::Candidate> ranked =
            gso::search(grid, factory, series, zeroCost(), totalReturnObjective);

    // Winner returns from its equity curve.
    EntryAtStrategy winner(4);
    BacktestResult run = backtester::run(winner, series, zeroCost());
    std::vector<double> returns;
    for (std::size_t i = 1; i < run.equityCurve().size(); i++) {
        returns.push_back(run.equityCurve()[i] / run.equityCurve()[i - 1] - 1);
    }
    double dsr = gso::deflatedSharpeOfWinner(ranked, returns, 252);
    EXPECT_TRUE(dsr >= 0.0 && dsr <= 1.0);

    std::vector<gso::Candidate> one(ranked.begin(), ranked.begin() + 1);
    EXPECT_THROWS(gso::deflatedSharpeOfWinner(one, returns, 252));
    EXPECT_THROWS(gso::deflatedSharpeOfWinner(ranked, {0.01, 0.02, 0.03}, 252));
    EXPECT_THROWS(gso::deflatedSharpeOfWinner(ranked, returns, 0));
}

// ----------------------------------------------------- WalkForwardAnalyzer

TEST(walkForwardFoldArithmeticAndStitchedEquity) {
    std::vector<double> closes(100, 100.0);
    BarSeries series = BarSeries::of("FLAT", closes);
    ParameterGrid grid;
    grid.add("mode", {0});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<ModeStrategy>(static_cast<int>(p.at("mode")));
    };

    wfa::WalkForwardResult result = wfa::analyze(series, grid, factory, zeroCost(), 30, 20,
                                                 totalReturnObjective);
    // Folds start at 0, 20, 40 (start + 50 <= 100).
    EXPECT_TRUE(result.folds.size() == 3);
    EXPECT_TRUE(result.folds[1].trainFrom == 20);
    EXPECT_TRUE(result.folds[1].trainTo == 50);
    EXPECT_TRUE(result.folds[1].testFrom == 50);
    EXPECT_TRUE(result.folds[1].testTo == 70);
    // One equity point per test bar, stitched across folds.
    EXPECT_TRUE(result.outOfSampleEquity.size() == 60);
    // Never trading on a flat tape: capital carries unchanged.
    for (double e : result.outOfSampleEquity) {
        EXPECT_NEAR(e, 100000.0, 1e-9);
    }
    // isSum = 0 -> the efficiency ratio is meaningless -> NaN.
    EXPECT_TRUE(std::isnan(result.efficiency));
}

TEST(walkForwardOptimizesOnTrainOnlyNoLookAhead) {
    // Train window rises, test window crashes. mode 1 (long) wins
    // in-sample; a leaky optimizer that saw the test window would pick
    // mode 0 and dodge the crash. The fold MUST pick mode 1 and eat the
    // out-of-sample loss — that honesty is the whole point.
    std::vector<double> closes(50);
    for (int i = 0; i < 30; i++) {
        closes[static_cast<std::size_t>(i)] = 100 + i;  // train: +29%
    }
    for (int i = 30; i < 50; i++) {
        closes[static_cast<std::size_t>(i)] = 129 - 3 * (i - 30);  // test: crash
    }
    BarSeries series = BarSeries::of("LEAK", closes);
    ParameterGrid grid;
    grid.add("mode", {0, 1});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<ModeStrategy>(static_cast<int>(p.at("mode")));
    };

    wfa::WalkForwardResult result = wfa::analyze(series, grid, factory, zeroCost(), 30, 20,
                                                 totalReturnObjective);
    EXPECT_TRUE(result.folds.size() == 1);
    EXPECT_NEAR(result.folds[0].bestParams.at("mode"), 1.0, 1e-12);
    EXPECT_TRUE(result.folds[0].inSampleObjective > 0);
    EXPECT_TRUE(result.folds[0].outOfSampleObjective < 0);
    EXPECT_TRUE(result.efficiency < 0);
    EXPECT_TRUE(result.outOfSampleTrades.size() == 1);
}

TEST(walkForwardFoldsAreWarmNotCold) {
    // SMA(2,3) crosses up right at the test boundary: only a fold warmed
    // on the train bars can catch it. A cold evaluation of the bare test
    // slice holds through its indicator warm-up and never enters.
    std::vector<double> closes = {10, 10, 10, 10, 10, 10, 10, 10, 10, 10,
                                  20, 20, 20, 20, 20, 20, 20, 20, 20, 20};
    BarSeries series = BarSeries::of("WARM", closes);
    ParameterGrid grid;
    grid.add("fast", {2});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<SmaCrossStrategy>(static_cast<int>(p.at("fast")), 3);
    };

    wfa::WalkForwardResult result = wfa::analyze(series, grid, factory, zeroCost(), 10, 10,
                                                 totalReturnObjective);
    EXPECT_TRUE(result.folds.size() == 1);
    // Warm fold: the cross at bar 10 (first test bar) is tradeable.
    EXPECT_TRUE(result.outOfSampleTrades.size() == 1);
    EXPECT_TRUE(result.outOfSampleTrades[0].entryIndex == 10);

    // Cold control: the bare test slice never trades.
    SmaCrossStrategy cold(2, 3);
    BarSeries coldSlice = series.slice(10, 20);
    BacktestResult coldResult = backtester::run(cold, coldSlice, zeroCost());
    EXPECT_TRUE(coldResult.trades().empty());
}

TEST(walkForwardGatesWindows) {
    std::vector<double> closes(40, 100.0);
    BarSeries series = BarSeries::of("G", closes);
    ParameterGrid grid;
    grid.add("mode", {0});
    StrategyFactory factory = [](const Params& p) {
        return std::make_unique<ModeStrategy>(static_cast<int>(p.at("mode")));
    };
    EXPECT_THROWS(wfa::analyze(series, grid, factory, zeroCost(), 5, 10,
                               totalReturnObjective));   // train < 10
    EXPECT_THROWS(wfa::analyze(series, grid, factory, zeroCost(), 30, 1,
                               totalReturnObjective));   // test < 2
    EXPECT_THROWS(wfa::analyze(series, grid, factory, zeroCost(), 30, 20,
                               totalReturnObjective));   // train + test > n
}

QTEST_MAIN()
