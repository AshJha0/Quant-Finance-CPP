#include "quantfinlib/backtest/validation/grid_search_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/validation/sharpe_validation.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace grid_search_optimizer {

std::vector<Candidate> search(const ParameterGrid& grid, const StrategyFactory& factory,
                              const BarSeries& series, const BacktestConfig& config,
                              const Objective& objective) {
    if (grid.size() == 0) {
        throw std::invalid_argument("empty parameter grid: nothing to search");
    }
    std::vector<Candidate> out;
    out.reserve(static_cast<std::size_t>(grid.size()));
    for (const Params& params : grid.combinations()) {
        std::unique_ptr<TradingStrategy> strategy = factory(params);
        BacktestResult result = backtester::run(*strategy, series, config);
        double score = objective(result.metrics());
        out.push_back(Candidate{params, result.metrics(),
                                std::isfinite(score)
                                        ? score
                                        : -std::numeric_limits<double>::infinity()});
    }
    // Descending by objective; stable like Java's TimSort, so ties keep
    // enumeration order.
    std::stable_sort(out.begin(), out.end(), [](const Candidate& a, const Candidate& b) {
        return a.objective > b.objective;
    });
    return out;
}

Candidate best(const ParameterGrid& grid, const StrategyFactory& factory,
               const BarSeries& series, const BacktestConfig& config,
               const Objective& objective) {
    return search(grid, factory, series, config, objective).front();
}

double deflatedSharpeOfWinner(const std::vector<Candidate>& ranked,
                              const std::vector<double>& winnerReturns, int periodsPerYear) {
    if (ranked.size() < 2) {
        throw std::invalid_argument("need >= 2 trials, got " + std::to_string(ranked.size()));
    }
    if (winnerReturns.size() < 4) {
        throw std::invalid_argument("need >= 4 winner returns, got "
                                    + std::to_string(winnerReturns.size()));
    }
    if (periodsPerYear <= 0) {
        throw std::invalid_argument("periodsPerYear must be > 0, got "
                                    + std::to_string(periodsPerYear));
    }
    // SharpeValidation works in per-period units; metrics store annualized.
    double perPeriodScale = std::sqrt(static_cast<double>(periodsPerYear));
    std::vector<double> trialSharpes(ranked.size(), 0.0);
    for (std::size_t i = 0; i < ranked.size(); i++) {
        double s = ranked[i].metrics.sharpeRatio / perPeriodScale;
        trialSharpes[i] = std::isfinite(s) ? s : 0;
    }
    double sd = math_utils::stdDev(winnerReturns);
    double observed = sd > 0 ? math_utils::mean(winnerReturns) / sd : 0;
    return sharpe_validation::deflatedSharpe(observed, trialSharpes,
                                             static_cast<int>(winnerReturns.size()),
                                             math_utils::skewness(winnerReturns),
                                             math_utils::kurtosis(winnerReturns));
}

}  // namespace grid_search_optimizer
}  // namespace quantfinlib
