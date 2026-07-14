// Port of com.quantfinlib.backtest.BenchmarkComparison: benchmark-relative
// performance (beta, annualized Jensen alpha at zero risk-free rate,
// tracking error, information ratio, up/down capture, active return).
// Up/down capture is NaN when the benchmark had no up (down) periods —
// no evidence, not zero. Series must be equal length, >= 3 periods, all
// finite, and the benchmark must actually vary; violations throw
// std::invalid_argument.
#pragma once

#include <vector>

namespace quantfinlib {
namespace benchmark_comparison {

struct Result {
    double alpha = 0;             // annualized Jensen alpha (risk-free = 0)
    double beta = 0;              // regression beta vs the benchmark
    double trackingError = 0;     // annualized stdev of active returns
    double informationRatio = 0;  // annualized active return / TE; 0 when TE is 0
    double upCapture = 0;         // capture over benchmark-up periods (NaN if none)
    double downCapture = 0;       // capture over benchmark-down periods (NaN if none)
    double activeReturn = 0;      // annualized mean of (strategy - benchmark)
};

/**
 * @param strategy       per-period strategy returns aligned with benchmark
 * @param benchmark      per-period benchmark returns, must vary
 * @param periodsPerYear annualization factor (252 daily, 12 monthly)
 */
Result compare(const std::vector<double>& strategy,
               const std::vector<double>& benchmark, int periodsPerYear);

}  // namespace benchmark_comparison
}  // namespace quantfinlib
