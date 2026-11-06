// Port of com.quantfinlib.pricing.SabrModel (Java): SABR stochastic
// volatility — Hagan et al. (2002) lognormal implied volatility
// approximation and smile calibration. With beta fixed (market
// convention), calibrates (alpha, rho, nu) to observed strike/vol quotes
// by seeded random search (java.util.SplittableRandom(42), reproduced
// bit-exactly by JavaSplittableRandom) plus shrinking coordinate
// refinement — derivative-free and deterministic.
#pragma once

#include <vector>

namespace quantfinlib {
namespace sabr_model {

/** Calibrated SABR parameters and the fit's RMSE in vol points. */
struct Params {
    double alpha;
    double beta;
    double rho;
    double nu;
    double rmse;
};

/** Hagan lognormal implied vol for forward f, strike k, expiry t. */
double impliedVol(double f, double k, double t,
                  double alpha, double beta, double rho, double nu);

/**
 * Smallest risk-neutral density d2C/dK2 (undiscounted Black call on the Hagan
 * vol) over n strikes in [kMin, kMax]. Hagan's expansion is not arbitrage-free:
 * at low strikes and long expiries a negative value means butterfly arbitrage.
 */
double minimumDensity(double f, double t, double alpha, double beta, double rho, double nu,
                      double kMin, double kMax, int n);

/**
 * Calibrates (alpha, rho, nu) with beta fixed, by seeded random search
 * plus shrinking coordinate refinement (derivative-free, deterministic).
 */
Params calibrate(double f, double t, double beta,
                 const std::vector<double>& strikes,
                 const std::vector<double>& marketVols);

}  // namespace sabr_model
}  // namespace quantfinlib
