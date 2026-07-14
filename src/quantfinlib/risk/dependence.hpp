// Port of com.quantfinlib.risk.Dependence (Java): rank-based dependence.
// Spearman's rho (Pearson on midranks), Kendall's tau-a (ties count as
// neither concordant nor discordant, O(n^2)), and the elliptical-copula
// bridge rho_pearson = sin(pi*tau/2).
#pragma once

#include <vector>

namespace quantfinlib {
namespace dependence {

/** Spearman rank correlation in [-1, 1]. */
double spearman(const std::vector<double>& a, const std::vector<double>& b);

/** Kendall's tau (tau-a) in [-1, 1]; O(n^2). */
double kendallTau(const std::vector<double>& a, const std::vector<double>& b);

/** The elliptical-copula bridge: Pearson rho implied by a Kendall tau. */
double pearsonFromKendall(double tau);

/** Midranks (average rank for ties), 1-based. */
std::vector<double> ranks(const std::vector<double>& values);

}  // namespace dependence
}  // namespace quantfinlib
