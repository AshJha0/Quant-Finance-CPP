// Port of com.quantfinlib.rates.ShortRateModels (Java): the three
// classic short-rate models with closed-form zero-coupon bond prices —
//   Vasicek    dr = a(b - r)dt + sigma dW        (Gaussian, rates can go
//              negative);
//   CIR        dr = a(b - r)dt + sigma sqrt(r)dW (non-negative; strictly
//              positive when the Feller condition 2ab >= sigma^2 holds);
//   Hull-White Vasicek with a time-dependent drift fitted so the model
//              reprices TODAY'S curve exactly; bond prices come from the
//              curve plus a Gaussian convexity adjustment — no explicit
//              theta(t) is needed for pricing.
// All prices are for unit face. Simulation steps: exact Gaussian for
// Vasicek, full-truncation Euler for CIR.
#pragma once

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace short_rate_models {

/** Vasicek zero-coupon bond price P(t, t+T) given the short rate now. */
double vasicekBond(double shortRate, double a, double b, double sigma,
                   double maturityYears);

/** The continuously-compounded zero yield implied by vasicekBond. */
double vasicekYield(double shortRate, double a, double b, double sigma,
                    double maturityYears);

/**
 * One EXACT Vasicek simulation step (the transition is Gaussian, so no
 * discretization error): r <- b + (r-b)e^{-a dt} + stdev*z.
 */
double vasicekStep(double shortRate, double a, double b, double sigma,
                   double dtYears, double gaussian);

/** CIR zero-coupon bond price P(t, t+T). */
double cirBond(double shortRate, double a, double b, double sigma,
               double maturityYears);

/** The Feller ratio 2ab/sigma^2; >= 1 keeps the CIR rate strictly positive. */
double cirFeller(double a, double b, double sigma);

/** One full-truncation Euler CIR step (never sources vol from a negative rate). */
double cirStep(double shortRate, double a, double b, double sigma,
               double dtYears, double gaussian);

/**
 * Hull-White zero-coupon bond price P(t, t+T) given the market curve and
 * the short rate now. At t = 0 with shortRate = f(0, 0) this reprices
 * the curve exactly; away from it, the Gaussian convexity adjustment:
 *   P(t,T) = [P(0,T)/P(0,t)] * exp(B(f(0,t) - r) - sigma^2 B^2 (1-e^{-2at})/(4a))
 */
double hullWhiteBond(const YieldCurve& curve, double tYears, double maturityYears,
                     double shortRate, double a, double sigma);

/**
 * The instantaneous forward rate f(0, t) off the curve, by symmetric
 * finite difference of ln P (the curve carries no analytic derivative);
 * a second-order one-sided stencil below one day, where a centered
 * window would clamp into a biased one-sided average.
 */
double instantaneousForward(const YieldCurve& curve, double tYears);

}  // namespace short_rate_models
}  // namespace quantfinlib
