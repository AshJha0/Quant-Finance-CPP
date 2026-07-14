// Port of com.quantfinlib.pricing.Heston (Java): Heston (1993)
// stochastic-volatility pricing. Variance follows its own mean-reverting
// square-root process, correlated with spot, so the model PRODUCES a
// smile. Pricing is semi-analytic: two probabilities recovered from the
// characteristic function via fixed-step Simpson integration, using the
// "little Heston trap" formulation (Albrecher et al. 2007) with the
// STABLE complex-sqrt form (imD = imD2/(2 reD) — the naive sqrt underflows
// the phase slope near u = 0 and biases ATM prices ~0.5%). The window and
// step count stretch with 1/(sigma_eff*sqrtT) so short-dated low-vol
// inputs are not silently truncated. Feller-violating parameters are
// accepted (markets calibrate there constantly); the Monte Carlo
// cross-check uses full-truncation Euler for exactly that reason, on a
// bit-exact java.util.Random stream.
#pragma once

#include <cstdint>

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace heston {

/** Model parameters. feller() tells you which regime you are in. */
struct Params {
    double kappa;
    double theta;
    double sigmaV;
    double rho;
    double v0;

    Params(double kappa_, double theta_, double sigmaV_, double rho_, double v0_);

    /** The Feller ratio 2*kappa*theta/sigmaV^2; >= 1 keeps variance positive. */
    double feller() const;
};

/** European call under Heston (semi-analytic). */
double call(double spot, double strike, double rate, double divYield,
            double timeYears, const Params& p);

/** European put via put-call parity. */
double put(double spot, double strike, double rate, double divYield,
           double timeYears, const Params& p);

/**
 * Full-truncation Euler Monte Carlo — the pricing cross-check (used by the
 * tests to validate the semi-analytic integral, and usable for payoffs the
 * closed form cannot reach). Deterministic per seed.
 */
double callMonteCarlo(double spot, double strike, double rate,
                      double divYield, double timeYears, const Params& p,
                      int steps, int paths, std::int64_t seed);

}  // namespace heston
}  // namespace quantfinlib
