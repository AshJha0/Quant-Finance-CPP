// Port of com.quantfinlib.crb.HedgeOptimizer (Java): cost-aware minimum-
// variance hedging. Minimizes
//   (e + L*h)' S (e + L*h)  +  lambda * sum_i c_i*|h_i|
// over hedge notionals h via cyclic coordinate descent with the exact
// soft-threshold update — deterministic, no external optimizer.
// lambda = 0 recovers the closed-form minimum-variance hedge. RELATIVE
// convergence tolerance; throws std::runtime_error on non-convergence or
// a non-PSD covariance (a negative diagonal L'SL term).
#pragma once

#include <vector>

namespace quantfinlib {
namespace hedge_optimizer {

using Matrix = std::vector<std::vector<double>>;

/**
 * @param exposures   factor exposures e (length n)
 * @param covariance  n x n factor covariance S
 * @param loadings    loadings[f][i] — factor f exposure created by one
 *                    unit of instrument i (n x m)
 * @param costPerUnit c_i >= 0 per unit |notional| (length m)
 * @param costWeight  lambda >= 0 — 0 is pure minimum variance
 * @return hedge notionals h (length m), signed
 */
std::vector<double> hedge(const std::vector<double>& exposures, const Matrix& covariance,
                          const Matrix& loadings, const std::vector<double>& costPerUnit,
                          double costWeight);

/** Post-hedge factor exposures e + L*h. */
std::vector<double> residual(const std::vector<double>& exposures, const Matrix& loadings,
                             const std::vector<double>& h);

/** Portfolio stdev of an exposure vector under S — the risk being cut. */
double risk(const std::vector<double>& exposures, const Matrix& covariance);

}  // namespace hedge_optimizer
}  // namespace quantfinlib
