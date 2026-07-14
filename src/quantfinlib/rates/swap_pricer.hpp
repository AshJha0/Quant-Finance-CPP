// Port of com.quantfinlib.rates.SwapPricer (Java): vanilla interest-rate
// swap pricing off the YieldCurve. Single-curve identities (annual fixed
// leg, matching the bootstrap's convention):
//   annuity    = sum DF(t_i)              i = 1..T (annual, tau = 1)
//   parRate    = (1 - DF(T)) / annuity    spot-starting
//   payerPv    = annuity * (parRate - K)  pay fixed K, receive float
//   receiverPv = -payerPv
// DV01 is the bump-and-reprice sensitivity to a parallel 1bp shift of
// the ZERO curve — annuity * e^z * 1bp on a flat cc curve (tests pin
// exactly that; the desk shorthand annuity * 1bp is a different
// derivative, ~e^z away).
// Stated simplifications: single curve, annual fixed leg, spot start.
#pragma once

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace swap_pricer {

/** PV of the annual fixed-leg annuity, per unit notional. */
double annuity(const YieldCurve& curve, int tenorYears);

/** The spot-starting par swap rate for `tenorYears`. */
double parRate(const YieldCurve& curve, int tenorYears);

/**
 * PV per unit notional of a PAYER swap (pay fixed `fixedRate`, receive
 * float). Negate for the receiver.
 */
double payerPv(const YieldCurve& curve, int tenorYears, double fixedRate);

/**
 * DV01 per unit notional: the payer swap's PV change for a +1bp parallel
 * shift of the zero curve (positive — rates up helps the fixed payer).
 */
double dv01(const YieldCurve& curve, int tenorYears, double fixedRate);

/** A copy of the curve with every pillar zero rate shifted by `bump`
 *  (Java: package-private static helper). */
YieldCurve parallelBump(const YieldCurve& curve, double bump);

}  // namespace swap_pricer
}  // namespace quantfinlib
