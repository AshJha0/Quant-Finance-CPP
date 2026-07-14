// Port of com.quantfinlib.backtest.validation.WalkForwardAnalyzer:
// walk-forward analysis — the standard defense against overfit
// backtests. The series is split into rolling train/test windows; on
// each fold the parameter grid is optimized ONLY on the train window
// and the winner is evaluated on the unseen test window. Out-of-sample
// test segments are stitched into one continuous equity curve (capital
// carries across folds), giving honest out-of-sample metrics and the
// walk-forward efficiency ratio (OOS / IS objective — near 1 is robust,
// near 0 is curve-fitting).
//
// Each test window is evaluated WARM: the backtest sees the preceding
// train bars for indicator warm-up but only trades from the test
// boundary (backtester::run with tradeFrom). Evaluating a bare test
// slice would re-compute every indicator cold and force HOLD through
// each fold's first lookback bars — systematically understating
// out-of-sample activity.
//
// The efficiency ratio is only meaningful when the in-sample objective
// sum is positive; when it is zero or negative (the optimizer could not
// find anything that even backtests well in-sample) efficiency is NaN —
// a ratio of two losses saying "0.5" would read as robust when both
// sides are failing. Gates map to std::invalid_argument.
#pragma once

#include <vector>

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/performance_metrics.hpp"
#include "quantfinlib/backtest/trade.hpp"
#include "quantfinlib/backtest/validation/grid_search_optimizer.hpp"
#include "quantfinlib/backtest/validation/parameter_grid.hpp"
#include "quantfinlib/backtest/validation/strategy_factory.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {
namespace walk_forward_analyzer {

struct Fold {
    int trainFrom = 0;
    int trainTo = 0;
    int testFrom = 0;
    int testTo = 0;
    Params bestParams;
    double inSampleObjective = 0;
    double outOfSampleObjective = 0;
};

struct WalkForwardResult {
    std::vector<Fold> folds;
    std::vector<double> outOfSampleEquity;
    PerformanceMetrics outOfSampleMetrics;
    std::vector<Trade> outOfSampleTrades;
    double efficiency = 0;
};

/**
 * @param trainBars bars in each optimization window
 * @param testBars  bars in each out-of-sample window; the window rolls
 *                  forward by this amount per fold
 */
WalkForwardResult analyze(const BarSeries& series, const ParameterGrid& grid,
                          const StrategyFactory& factory, const BacktestConfig& config,
                          int trainBars, int testBars,
                          const grid_search_optimizer::Objective& objective);

}  // namespace walk_forward_analyzer
}  // namespace quantfinlib
