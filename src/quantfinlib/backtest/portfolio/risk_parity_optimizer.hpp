// Port of com.quantfinlib.optimization.RiskParityOptimizer (placed under
// backtest/portfolio in this port): equal-risk-contribution weights via
// the multiplicative fixed-point iteration on marginal risk
// contributions. Deterministic, long-only. Zero-variance assets are
// refused (std::invalid_argument); a fixed point that fails to converge
// in 10,000 iterations throws std::runtime_error (IllegalStateException
// in Java) rather than returning a stalled iterate.
#pragma once

#include <vector>

#include "quantfinlib/backtest/portfolio/portfolio_optimizer.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace risk_parity_optimizer {

/** Equal-risk-contribution weights (expected returns used only for reporting). */
Allocation equalRiskContribution(const std::vector<double>& expectedReturns,
                                 const math_utils::Matrix& covariance);

/** Each asset's fractional contribution to portfolio variance under weights w. */
std::vector<double> riskContributions(const std::vector<double>& w,
                                      const math_utils::Matrix& covariance);

}  // namespace risk_parity_optimizer
}  // namespace quantfinlib
