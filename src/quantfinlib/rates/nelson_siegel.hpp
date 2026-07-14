// Port of com.quantfinlib.rates.NelsonSiegel (Java): parametric
// yield-curve fit z(t) = b0 + b1*(1-e^{-t/l})/(t/l)
//                          + b2*[(1-e^{-t/l})/(t/l) - e^{-t/l}].
// b0 = LEVEL (z at infinity), b1 = SLOPE (z(0) = b0 + b1; b1 > 0 is
// inversion), b2 = CURVATURE (mid-curve hump near t ~ lambda).
//
// Fitting exploits linearity in (b0, b1, b2) for FIXED lambda: a
// log-spaced grid search over lambda (0.1y-10y, 80 nodes) with an exact
// 3x3 OLS solve at each node — deterministic, no local-minimum roulette.
// Betas are NOT sign-constrained: an inverted curve is data, not an error.
#pragma once

#include <vector>

namespace quantfinlib {
namespace nelson_siegel {

/** Fitted parameters plus the fit's root-mean-square error (Java record Fit). */
struct Fit {
    double b0;
    double b1;
    double b2;
    double lambda;
    double rmse;

    /** Model zero rate at tenor `t` years, > 0. */
    double zeroRate(double t) const;

    /** The short-rate limit z(0+) = b0 + b1 (exact in the model). */
    double shortRate() const { return b0 + b1; }

    /** The long-end asymptote z(infinity) = b0. */
    double longRate() const { return b0; }
};

/**
 * Fits by log-spaced lambda grid (0.1y-10y, 80 nodes) + exact OLS per node.
 * tenorYears: observation tenors, >= 4 distinct, all > 0;
 * zeroRates: observed zero rates (continuously compounded).
 */
Fit fit(const std::vector<double>& tenorYears, const std::vector<double>& zeroRates);

}  // namespace nelson_siegel
}  // namespace quantfinlib
