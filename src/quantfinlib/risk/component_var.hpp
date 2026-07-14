// Port of com.quantfinlib.risk.ComponentVar (Java): Euler allocation of
// delta-normal VaR. sigma_p = sqrt(w'Sw), marginal_i = z*(Sw)_i/sigma_p,
// component_i = w_i * marginal_i, and components sum EXACTLY to
// portfolio VaR — no diversification residual. Incremental VaR is a
// full re-computation with the position closed (NOT component VaR: a
// hedge has positive size, negative component, and closing it RAISES
// VaR). Delta-normal only; VaR reported positive; components signed.
#pragma once

#include <vector>

namespace quantfinlib {
namespace component_var {

using Matrix = std::vector<std::vector<double>>;

/**
 * portfolioVar: total delta-normal VaR (positive).
 * components:   per-position component VaR; sums exactly to portfolioVar;
 *               hedges are negative.
 * marginals:    per-position marginal VaR (z * (Sw)_i / sigma_p).
 */
struct Allocation {
    double portfolioVar;
    std::vector<double> components;
    std::vector<double> marginals;
};

/**
 * Euler allocation of delta-normal VaR.
 *
 * @param weights    position exposures (currency units), signed
 * @param covariance return covariance matrix, symmetric, n x n
 * @param confidence e.g. 0.99; z = Phi^-1(confidence)
 */
Allocation allocate(const std::vector<double>& weights, const Matrix& covariance,
                    double confidence);

/**
 * Incremental VaR of position i: portfolio VaR now minus VaR with the
 * position closed (weight zeroed). Positive means closing it REDUCES
 * risk; negative means the position is a hedge.
 */
double incremental(const std::vector<double>& weights, const Matrix& covariance,
                   double confidence, int i);

}  // namespace component_var
}  // namespace quantfinlib
