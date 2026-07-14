// Port of com.quantfinlib.optimization.PortfolioOptimizer (placed under
// backtest/portfolio in this port): long-only fully-invested portfolio
// optimization — max Sharpe, min volatility, efficient frontier — via
// stochastic Dirichlet sampling over the simplex plus deterministic
// pairwise-transfer refinement (no external solver). Deterministic per
// seed; the sampling stream is the Java-exact SplittableRandom, so
// seeded results match the Java library. Expected returns and covariance
// must share the same periodicity.
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

/** An optimized allocation with its risk/return profile (input periodicity). */
struct Allocation {
    std::vector<double> weights;
    double expectedReturn = 0;
    double volatility = 0;
    double sharpe = 0;
};

class PortfolioOptimizer {
public:
    PortfolioOptimizer(std::vector<double> expectedReturns, math_utils::Matrix covariance,
                       std::int64_t seed = 42);

    /** Maximum Sharpe ratio portfolio; riskFreeRate in the inputs' periodicity. */
    Allocation maxSharpe(double riskFreeRate) const;

    /** Minimum volatility portfolio. */
    Allocation minVolatility() const;

    /**
     * Efficient frontier: minimum-volatility portfolios across a grid of
     * target returns between the min and max asset expected returns.
     */
    std::vector<Allocation> efficientFrontier(int points) const;

    /** Rebalancing deltas: target minus current weights, per asset. */
    static std::vector<double> rebalance(const std::vector<double>& currentWeights,
                                         const std::vector<double>& targetWeights);

private:
    static constexpr int SAMPLES = 20000;
    static constexpr int REFINE_SWEEPS = 60;

    std::vector<double> optimize(
            const std::function<double(const std::vector<double>&)>& objective) const;
    double volatilityOf(const std::vector<double>& w) const;
    Allocation toAllocation(std::vector<double> w, double riskFreeRate) const;

    std::vector<double> expectedReturns_;
    math_utils::Matrix covariance_;
    std::int64_t seed_;
};

}  // namespace quantfinlib
