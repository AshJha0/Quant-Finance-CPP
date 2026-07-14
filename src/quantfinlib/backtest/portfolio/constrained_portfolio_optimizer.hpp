// Port of com.quantfinlib.optimization.ConstrainedPortfolioOptimizer
// (placed under backtest/portfolio in this port): long-only optimizer
// with per-asset weight bounds (position caps/floors) and an optional
// turnover penalty against current holdings —
// adjusted return = mu.w - penalty * sum|w - w_current| — using the same
// derivative-free search as PortfolioOptimizer with feasibility
// projection onto the box-constrained simplex. Bounds that admit no
// fully-invested portfolio throw std::invalid_argument.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "quantfinlib/backtest/portfolio/portfolio_optimizer.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

class ConstrainedPortfolioOptimizer {
public:
    ConstrainedPortfolioOptimizer(std::vector<double> expectedReturns,
                                  math_utils::Matrix covariance, std::int64_t seed = 42);

    /** Per-asset weight bounds; must admit a fully-invested portfolio. */
    ConstrainedPortfolioOptimizer& withBounds(const std::vector<double>& min,
                                              const std::vector<double>& max);

    /**
     * @param penaltyPerUnitTurnover expected-return units charged per unit of
     *                               one-way turnover (e.g. transaction cost)
     */
    ConstrainedPortfolioOptimizer& withTurnoverPenalty(
            const std::vector<double>& currentWeights, double penaltyPerUnitTurnover);

    Allocation maxSharpe(double riskFreeRate) const;
    Allocation minVolatility() const;

private:
    double adjustedReturn(const std::vector<double>& w) const;
    double turnoverCost(const std::vector<double>& w) const;
    std::vector<double> optimize(
            const std::function<double(const std::vector<double>&)>& objective) const;
    std::vector<double> project(std::vector<double> w) const;
    Allocation toAllocation(std::vector<double> w, double riskFreeRate) const;

    std::vector<double> expectedReturns_;
    math_utils::Matrix covariance_;
    std::int64_t seed_;
    std::vector<double> minWeights_;
    std::vector<double> maxWeights_;
    std::vector<double> currentWeights_;  // empty = no turnover anchor
    double turnoverPenalty_ = 0;
};

}  // namespace quantfinlib
