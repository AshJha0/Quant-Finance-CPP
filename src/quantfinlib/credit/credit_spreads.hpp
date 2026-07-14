// Port of com.quantfinlib.credit.CreditSpreads (Java): bond credit-spread
// measures. The Z-SPREAD is the single constant shift z added to every
// point of the risk-free zero curve that makes the bond's discounted
// cash flows equal its dirty price:
//   price = sum cf_i * exp(-(z(t_i) + z) * t_i)
// Solving is bisection on z in [-50%, +500%] with an explicit bracket
// check: a price outside what that range can explain throws rather than
// returning an endpoint (the house rule for every solver). Annual-
// fraction period grid i/frequency, standard fixed-coupon bond cash flows.
#pragma once

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace credit_spreads {

/**
 * The Z-spread (continuously compounded, decimal) of a fixed-coupon bond
 * over `curve`.
 * dirtyPrice: market dirty price per `face`; face > 0; couponRate:
 * annual coupon rate (decimal); frequency: coupons per year, >= 1;
 * yearsToMaturity: whole periods assumed, > 0.
 */
double zSpread(double dirtyPrice, double face, double couponRate,
               int frequency, double yearsToMaturity, const YieldCurve& curve);

/** Bond PV under the curve shifted by a constant z (cc). */
double priceWithZSpread(double face, double couponRate, int frequency,
                        double yearsToMaturity, const YieldCurve& curve, double z);

}  // namespace credit_spreads
}  // namespace quantfinlib
