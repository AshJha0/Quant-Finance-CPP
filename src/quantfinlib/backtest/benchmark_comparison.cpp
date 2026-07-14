#include "quantfinlib/backtest/benchmark_comparison.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/backtest/risk_helpers.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace benchmark_comparison {

Result compare(const std::vector<double>& strategy,
               const std::vector<double>& benchmark, int periodsPerYear) {
    if (strategy.size() != benchmark.size()) {
        throw std::invalid_argument("length mismatch: strategy="
                + std::to_string(strategy.size())
                + " benchmark=" + std::to_string(benchmark.size()));
    }
    int n = static_cast<int>(strategy.size());
    if (n < 3) {
        throw std::invalid_argument("need >= 3 aligned periods, got " + std::to_string(n));
    }
    if (periodsPerYear <= 0) {
        throw std::invalid_argument("periodsPerYear must be > 0, got "
                                    + std::to_string(periodsPerYear));
    }
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(strategy[i]) || !std::isfinite(benchmark[i])) {
            throw std::invalid_argument("non-finite return at period " + std::to_string(i));
        }
    }
    double varB = math_utils::variance(benchmark);
    if (!(varB > 0)) {
        throw std::invalid_argument("benchmark returns carry no variance");
    }

    double beta = backtest_detail::beta(strategy, benchmark);
    double meanS = math_utils::mean(strategy);
    double meanB = math_utils::mean(benchmark);
    double alpha = (meanS - beta * meanB) * periodsPerYear;

    std::vector<double> active(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        active[i] = strategy[i] - benchmark[i];
    }
    double activeAnn = math_utils::mean(active) * periodsPerYear;
    double te = math_utils::stdDev(active) * std::sqrt(static_cast<double>(periodsPerYear));
    double ir = te > 0 ? activeAnn / te : 0;

    double upS = 0, upB = 0, downS = 0, downB = 0;
    int ups = 0, downs = 0;
    for (int i = 0; i < n; i++) {
        if (benchmark[i] > 0) {
            upS += strategy[i];
            upB += benchmark[i];
            ups++;
        } else if (benchmark[i] < 0) {
            downS += strategy[i];
            downB += benchmark[i];
            downs++;
        }
    }
    double nan = std::numeric_limits<double>::quiet_NaN();
    double upCapture = ups == 0 ? nan : (upS / ups) / (upB / ups);
    double downCapture = downs == 0 ? nan : (downS / downs) / (downB / downs);

    Result r;
    r.alpha = alpha;
    r.beta = beta;
    r.trackingError = te;
    r.informationRatio = ir;
    r.upCapture = upCapture;
    r.downCapture = downCapture;
    r.activeReturn = activeAnn;
    return r;
}

}  // namespace benchmark_comparison
}  // namespace quantfinlib
