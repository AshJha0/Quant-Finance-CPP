// Port of com.quantfinlib.pricing.ExchangeOption (Java): TWO-ASSET closed
// forms. Margrabe (1978) — the right to exchange asset 2 for asset 1 is a
// Black-Scholes call in disguise: the rate drops out and the vol is the
// vol of the RATIO, sigma^2 = vol1^2 + vol2^2 - 2*rho*vol1*vol2. Kirk
// (1995) — spread call max(0, F1 - F2 - K) approximated by treating
// F2 + K as one lognormal with vol scaled by its F2 share; exact in both
// limits (K = 0 collapses to Margrabe, F2 = 0 to Black-76, both pinned).
#pragma once

namespace quantfinlib {
namespace exchange_option {

/**
 * Margrabe: receive asset 1, deliver asset 2 at expiry.
 * q1/q2 are the assets' continuous yields; rho the log-return correlation.
 */
double margrabe(double s1, double s2, double q1, double q2,
                double vol1, double vol2, double rho, double timeYears);

/**
 * Kirk's approximation for a spread CALL on two forwards:
 * max(0, F1 - F2 - K) paid at expiry, discounted at rate. strike may be 0
 * (Margrabe limit) but not negative — flip the legs instead.
 */
double kirkSpreadCall(double f1, double f2, double strike, double rate,
                      double vol1, double vol2, double rho, double timeYears);

}  // namespace exchange_option
}  // namespace quantfinlib
