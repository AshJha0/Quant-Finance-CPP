// Port of com.quantfinlib.optimization.BlackLitterman (placed under
// backtest/portfolio in this port): market-implied equilibrium returns
// (reverse optimization) blended with investor views at explicit
// confidences. Posterior:
//   mu = [(tau*Sigma)^-1 + P' Omega^-1 P]^-1 [(tau*Sigma)^-1 Pi + P' Omega^-1 Q]
// with pick matrix P (one row per view), view returns Q, and diagonal
// view variances Omega (smaller = more confident). No views returns the
// equilibrium unchanged; non-positive view variance throws
// std::invalid_argument.
#pragma once

#include <vector>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace black_litterman {

/** Equilibrium (implied) returns from the market portfolio: Pi = delta*Sigma*w_mkt. */
std::vector<double> impliedEquilibriumReturns(double riskAversion,
                                              const math_utils::Matrix& covariance,
                                              const std::vector<double>& marketWeights);

/**
 * Posterior expected returns blending equilibrium and views.
 *
 * @param tau       uncertainty scaling of the prior (typically 0.01-0.05)
 * @param p         pick matrix [views][assets]; empty = no views
 * @param q         expected return of each view
 * @param omegaDiag variance (uncertainty) of each view
 */
std::vector<double> posteriorReturns(double tau, const math_utils::Matrix& covariance,
                                     const std::vector<double>& equilibriumReturns,
                                     const math_utils::Matrix& p,
                                     const std::vector<double>& q,
                                     const std::vector<double>& omegaDiag);

}  // namespace black_litterman
}  // namespace quantfinlib
