// Port of com.quantfinlib.rates.BondPricer (Java): fixed-coupon bond
// analytics — price/yield conversion, Macaulay and modified duration,
// convexity, DV01. Yields are per-annum with `frequency` compounding.
//
// A maturity that is not a whole number of coupon periods has a short first
// period: with x = years*frequency, n = ceil(x) coupons fall at k - 1 + w
// periods from now, w = x - (n - 1), and a fraction 1 - w of the first coupon
// has accrued. Whole periods are the case w = 1. The date-based section uses
// street convention: actual coupon dates from the maturity anchor, a DayCount
// for accrued and the first fractional discount period, and periodic
// compounding at the yield. Yield-to-maturity is the single rate.
// The YTM solver is bisection on [-90%, 1000%] with an explicit bracket
// check — a price outside the attainable range throws rather than
// silently converging to an endpoint.
#pragma once

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/day_count.hpp"
#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace bond_pricer {

/** Dirty price per `face` from a yield, stub first period included. */
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

/** Interest accrued in the current period: coupon * (1 - w); 0 on whole periods. */
double accruedInterest(double face, double couponRate, int frequency, double yearsToMaturity);

/** Dirty price on a dated schedule rolled back from maturity. */
double dirtyPrice(double face, double couponRate, int frequency, const LocalDate& settlement,
                  const LocalDate& maturity, double yield, DayCount dayCount,
                  bool endOfMonth = false);

/** Accrued interest at settlement on the dated schedule. */
double accruedInterest(double face, double couponRate, int frequency,
                       const LocalDate& settlement, const LocalDate& maturity,
                       DayCount dayCount, bool endOfMonth = false);

/** dirtyPrice - accruedInterest. */
double cleanPrice(double face, double couponRate, int frequency, const LocalDate& settlement,
                  const LocalDate& maturity, double yield, DayCount dayCount,
                  bool endOfMonth = false);

/** Yield from a clean price by bisection; throws outside [-90%, 1000%]. */
double yieldFromCleanPrice(double cleanPriceValue, double face, double couponRate, int frequency,
                           const LocalDate& settlement, const LocalDate& maturity,
                           DayCount dayCount, bool endOfMonth = false);

/** Settlement date: tradeDate plus lagDays business days. */
LocalDate settlementDate(const LocalDate& tradeDate, int lagDays,
                         const BusinessCalendar& calendar);

}  // namespace bond_pricer
}  // namespace quantfinlib
