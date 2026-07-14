// Port of com.quantfinlib.backtest.validation.GridSearchOptimizer:
// exhaustive strategy parameter search — backtests every grid
// combination and ranks by an objective (e.g. the Sharpe ratio).
// Non-finite objectives rank last. Feed the resulting in-sample winners
// into WalkForwardAnalyzer — never trust them raw. The Java
// ToDoubleFunction<PerformanceMetrics> objective becomes a
// std::function. Gates map to std::invalid_argument.
#pragma once

#include <functional>
#include <vector>

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/performance_metrics.hpp"
#include "quantfinlib/backtest/validation/parameter_grid.hpp"
#include "quantfinlib/backtest/validation/strategy_factory.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {
namespace grid_search_optimizer {

using Objective = std::function<double(const PerformanceMetrics&)>;

struct Candidate {
    Params params;
    PerformanceMetrics metrics;
    double objective = 0;
};

std::vector<Candidate> search(const ParameterGrid& grid, const StrategyFactory& factory,
                              const BarSeries& series, const BacktestConfig& config,
                              const Objective& objective);

/** The winning parameter set only. */
Candidate best(const ParameterGrid& grid, const StrategyFactory& factory,
               const BarSeries& series, const BacktestConfig& config,
               const Objective& objective);

/**
 * The MULTIPLE-TESTING HAIRCUT for the grid's winner: the probability
 * that the top-ranked candidate's Sharpe beats what the best of
 * ranked.size() zero-skill trials would have scored anyway
 * (sharpe_validation::deflatedSharpe). A grid search computes a Sharpe
 * for every trial and then quietly reports only the maximum — this is
 * the one number that makes that selection honest. Values near 1 mean
 * the winner survives its own search; below ~0.95 the "best" parameter
 * set is indistinguishable from picking the luckiest of N random ones.
 *
 * @param ranked         result of search() (uses every trial's Sharpe
 *                       as the null distribution), >= 2 trials
 * @param winnerReturns  the winner's per-period returns (derive from
 *                       its equity curve), >= 4 observations
 * @param periodsPerYear the annualization used by the backtest metrics
 */
double deflatedSharpeOfWinner(const std::vector<Candidate>& ranked,
                              const std::vector<double>& winnerReturns, int periodsPerYear);

}  // namespace grid_search_optimizer
}  // namespace quantfinlib
