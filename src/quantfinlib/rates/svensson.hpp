// Port of com.quantfinlib.rates.Svensson (Java): Nelson-Siegel-Svensson
// yield-curve fit — Nelson-Siegel with a SECOND curvature hump:
//   z(t) = b0 + b1*f1(t/l1) + b2*f2(t/l1) + b3*f2(t/l2)
//   f1(x) = (1 - e^-x)/x        f2(x) = (1 - e^-x)/x - e^-x
//
// Fitting mirrors NelsonSiegel exactly: for FIXED (l1, l2) the model is
// LINEAR in (b0, b1, b2, b3), so the fit is a 2-D log-spaced grid over
// the lambdas (0.1y-10y, 50 nodes per axis) with an exact 4-regressor
// OLS solve per node. Nodes with lambda2 <= lambda1 are skipped: the two
// f2 regressors collide as the lambdas meet, and the ordering makes the
// parameterization identifiable. Betas are NOT sign-constrained.
#pragma once

#include <vector>

namespace quantfinlib {
namespace svensson {

/** Fitted parameters plus the fit's root-mean-square error (Java record Fit). */
struct Fit {
    double b0;
    double b1;
    double b2;
    double b3;
    double lambda1;
    double lambda2;
    double rmse;

    /** Model zero rate at tenor `t` years, > 0. */
    double zeroRate(double t) const;

    /** The short-rate limit z(0+) = b0 + b1 (exact in the model). */
    double shortRate() const { return b0 + b1; }

    /** The long-end asymptote z(infinity) = b0. */
    double longRate() const { return b0; }
};

/**
 * Fits by 2-D log-spaced lambda grid (0.1y-10y, 50 nodes per axis,
 * lambda2 > lambda1 only) + exact 4-regressor OLS per node.
 * tenorYears: observation tenors, >= 6 distinct, all > 0.
 */
Fit fit(const std::vector<double>& tenorYears, const std::vector<double>& zeroRates);

}  // namespace svensson
}  // namespace quantfinlib
