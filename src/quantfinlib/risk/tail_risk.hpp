// Port of com.quantfinlib.risk.TailRisk (Java): empirical VaR and expected
// shortfall of a loss sample, the one tail definition var_engine, frtb_es and
// risk_metrics share. Inputs are losses, positive = loss; c is the one-sided
// level. With losses sorted ascending L(1) <= ... <= L(n):
//   VaR = L(ceil(c*n)), the lower empirical quantile.
//   ES  = (L(n) + ... + L(n-k+1) + (w-k)*L(n-k)) / w, w = (1-c)*n, k = floor(w),
// the Acerbi-Tasche estimator ("On the coherence of expected shortfall",
// J. Banking and Finance 26, 2002, Prop. 4.1). It never falls below VaR, and
// when w <= 1 it is the worst loss. Example: losses 1..1000 at c = 0.975 give
// w = 25, VaR = 975 and ES = 988. c*n and (1-c)*n snap to the nearest integer
// within 1e-9. Results are not clamped: a tail of gains is negative.
#pragma once

#include <vector>

namespace quantfinlib {
namespace tail_risk {

struct Tail {
    double var;
    double expectedShortfall;
};

/** VaR and ES of an unsorted loss sample. Throws on empty or non-finite input. */
Tail of(const std::vector<double>& losses, double confidence);

/** VaR and ES of a sample already sorted ascending. */
Tail ofSorted(const std::vector<double>& sortedLosses, double confidence);

}  // namespace tail_risk
}  // namespace quantfinlib
