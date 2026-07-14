// Port of com.quantfinlib.rates.RatesOptions (Java): RATES VOLATILITY
// products priced off the curve.
//   Swaption — an option on a forward-starting swap: the underlying is
//   the forward swap rate F = (DF(start) - DF(end)) / annuity, the
//   natural numeraire is the ANNUITY, and the price is simply
//   annuity * Black76(F, K, vol, expiry) — discounting lives entirely in
//   the annuity, which is why the Black-76 leg is called with rate 0.
//   A PAYER swaption (right to pay fixed) is a CALL on the swap rate.
//   Cap / floor — a strip of independent Black-76 caplets (floorlets),
//   one per accrual period, on that period's SIMPLE forward rate
//   f_i = (DF(t_{i-1})/DF(t_i) - 1)/tau, fixing at period START, paying
//   at its end. The first period's rate is already fixed today, so its
//   caplet is pure intrinsic — included (market convention).
// Two identities keep this honest (both pinned by tests): payer -
// receiver = annuity*(F - K), and cap - floor = the matching swap's PV.
// Annual fixed legs and accrual periods (tau = 1); one flat lognormal
// vol per product.
//
// Note: the Java class delegates to pricing.Black76; the pricing domain
// is not part of this port, so rates_options.cpp carries a file-local
// copy of the Black-76 price formula (coefficients identical).
#pragma once

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace rates_options {

/** PV of 1 per year paid annually over (startYears, startYears+tenorYears]. */
double annuity(const YieldCurve& curve, int startYears, int tenorYears);

/** Forward par swap rate for a swap starting at `startYears`. */
double forwardSwapRate(const YieldCurve& curve, int startYears, int tenorYears);

/**
 * Black-76 swaption price per unit notional.
 * payer: true = payer swaption (call on the swap rate);
 * vol: flat lognormal vol of the forward swap rate.
 */
double swaption(const YieldCurve& curve, int startYears, int tenorYears,
                double strike, double vol, bool payer);

/** Cap: strip of annual Black-76 caplets to `maturityYears`. */
double cap(const YieldCurve& curve, int maturityYears, double strike, double vol);

/** Floor: the matching strip of floorlets. */
double floor(const YieldCurve& curve, int maturityYears, double strike, double vol);

}  // namespace rates_options
}  // namespace quantfinlib
