#include "quantfinlib/backtest/validation/walk_forward_analyzer.hpp"

#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/performance_analytics.hpp"

namespace quantfinlib {
namespace walk_forward_analyzer {

WalkForwardResult analyze(const BarSeries& series, const ParameterGrid& grid,
                          const StrategyFactory& factory, const BacktestConfig& config,
                          int trainBars, int testBars,
                          const grid_search_optimizer::Objective& objective) {
    int n = series.size();
    if (trainBars < 10 || testBars < 2 || trainBars + testBars > n) {
        throw std::invalid_argument("invalid windows: train=" + std::to_string(trainBars)
                                    + " test=" + std::to_string(testBars)
                                    + " bars=" + std::to_string(n));
    }
    std::vector<Fold> folds;
    std::vector<double> oosEquity;
    std::vector<Trade> oosTrades;
    double carryCapital = config.initialCapital;
    double isSum = 0, oosSum = 0;

    for (int start = 0; start + trainBars + testBars <= n; start += testBars) {
        int trainTo = start + trainBars;
        int testTo = trainTo + testBars;

        BarSeries train = series.slice(start, trainTo);
        grid_search_optimizer::Candidate best =
                grid_search_optimizer::best(grid, factory, train, config, objective);

        // Warm-up = the train window; trading starts at the test boundary.
        BarSeries warmPlusTest = series.slice(start, testTo);
        std::unique_ptr<TradingStrategy> strategy = factory(best.params);
        BacktestResult oos = backtester::run(*strategy, warmPlusTest,
                                             config.withInitialCapital(carryCapital), trainBars);
        double oosScore = objective(oos.metrics());

        folds.push_back(Fold{start, trainTo, trainTo, testTo, best.params, best.objective,
                             oosScore});
        oosEquity.insert(oosEquity.end(), oos.equityCurve().begin(), oos.equityCurve().end());
        oosTrades.insert(oosTrades.end(), oos.trades().begin(), oos.trades().end());
        carryCapital = oos.metrics().finalEquity;
        isSum += best.objective;
        oosSum += oosScore;
    }
    if (folds.empty()) {
        throw std::invalid_argument("series too short for even one fold");
    }
    PerformanceMetrics metrics =
            performance_analytics::compute(oosEquity, oosTrades, config.periodsPerYear);
    double efficiency =
            isSum > 0 ? oosSum / isSum : std::numeric_limits<double>::quiet_NaN();
    return WalkForwardResult{std::move(folds), std::move(oosEquity), metrics,
                             std::move(oosTrades), efficiency};
}

}  // namespace walk_forward_analyzer
}  // namespace quantfinlib
