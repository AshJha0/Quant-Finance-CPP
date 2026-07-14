// Port of com.quantfinlib.rates.BondPricer (Java): fixed-coupon bond
// analytics — price/yield conversion, Macaulay and modified duration,
// convexity, DV01. Yields are per-annum with `frequency` compounding.
//
// Simplifications, stated (copied from the Java doc): whole coupon
// periods (no accrued-interest split of dirty into clean price), no
// settlement-lag discounting, yield-to-maturity as the single rate.
// The YTM solver is bisection on [-90%, 1000%] with an explicit bracket
// check — a price outside the attainable range throws rather than
// silently converging to an endpoint.
//
// NOT ported: the date-based section (dirtyPrice/accruedInterest/
// cleanPrice/settlementDate) — it depends on DayCount and
// BusinessCalendar, which are outside this port's scope.
#pragma once

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace bond_pricer {

/** Dirty price per `face` from a yield (regular schedule, whole periods). */
double priceFromYield(double face, double couponRate, int frequency,
                      double yearsToMaturity, double yield);

/** Price by discounting each cash flow on a zero curve. */
double priceFromCurve(double face, double couponRate, int frequency,
                      double yearsToMaturity, const YieldCurve& curve);

/** Yield to maturity by bisection (price must be positive). */
double yieldToMaturity(double price, double face, double couponRate,
                       int frequency, double yearsToMaturity);

/** Macaulay duration in years: PV-weighted average time to cash flow. */
double macaulayDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield);

/** Modified duration: price sensitivity per unit yield change. */
double modifiedDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield);

/** Convexity (numeric second derivative of price w.r.t. yield, normalized by price). */
double convexity(double face, double couponRate, int frequency,
                 double yearsToMaturity, double yield);

/** Price change for a one-basis-point yield move (positive number). */
double dv01(double face, double couponRate, int frequency,
            double yearsToMaturity, double yield);

}  // namespace bond_pricer
}  // namespace quantfinlib
