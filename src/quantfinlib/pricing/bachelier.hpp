// Port of com.quantfinlib.pricing.Bachelier (Java): the normal model for
// options on a forward. dF = sigma_N dW, so the forward can go negative and
// sigma_N is in absolute rate units (0.0080 = 80 bp a year). Rates desks quote
// swaptions and caps this way since negative rates broke the lognormal model
// (Bachelier 1900; Andersen and Piterbarg 2010, vol. 1, section 7.1).
//   call = df * [(F - K) N(d) + sigma_N sqrt(T) n(d)],  d = (F - K)/(sigma_N sqrt(T))
//   put  = df * [(K - F) N(-d) + sigma_N sqrt(T) n(d)]
// Forward and strike may be any finite numbers. `rate` is the continuously
// compounded discount rate to payment; pass 0 and multiply by an annuity for
// swaptions.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace bachelier {

/** Discounted normal-model price. Throws on non-finite input or negative vol. */
double price(OptionType type, double forward, double strike,
             double rate, double normalVol, double timeYears);

/** Sensitivity to the forward: df*N(d) for a call, -df*N(-d) for a put. */
double delta(OptionType type, double forward, double strike,
             double rate, double normalVol, double timeYears);

/** Vega per 1.00 of normal vol: df*sqrt(T)*n(d). */
double vega(double forward, double strike, double rate,
            double normalVol, double timeYears);

/**
 * Implied normal vol from a discounted price: NaN for a price below intrinsic
 * or invalid input, 0 at intrinsic. Out of the money the time value is
 * v = sigma*sqrt(T)*h(u), u = |F-K|/(sigma*sqrt(T)), h(u) = n(u) - u*N(-u)
 * (Jaeckel 2017, "Implied normal volatility"); a bracketed Newton iteration
 * runs on ln(h(u)/u), which is smooth and monotone.
 */
double impliedVol(OptionType type, double price, double forward,
                  double strike, double rate, double timeYears);

}  // namespace bachelier
}  // namespace quantfinlib
