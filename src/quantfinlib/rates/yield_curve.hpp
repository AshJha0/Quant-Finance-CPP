// Port of com.quantfinlib.rates.YieldCurve (Java): zero-coupon yield
// curve storing continuously-compounded ZERO rates at pillar tenors.
// DF(t) = e^{-z(t)*t}; the implied forward between t1 and t2 falls out of
// the ratio of discount factors. Bootstrapping from annual par swaps uses
// DF_n = (1 - parRate_n * A_{n-1}) / (1 + parRate_n) with A the annuity
// so far, and reprices every input exactly (tested).
//
// Model choices, stated (copied from the Java doc): linear interpolation
// ON ZERO RATES, flat extrapolation beyond the pillars, one curve for
// both discounting and projection. Java's TreeMap maps to std::map.
#pragma once

#include <map>
#include <vector>

namespace quantfinlib {

class YieldCurve {
public:
    /** Curve from parallel arrays of tenors (years) and continuous zero rates. */
    static YieldCurve ofZeroRates(const std::vector<double>& tenorYears,
                                  const std::vector<double>& zeroRatesCc);

    /**
     * Classic bootstrap from par swap rates with an annual fixed leg at
     * integer-year pillars (missing years are filled by linear
     * interpolation of the par rates):
     * DF_n = (1 - parRate_n * A_{n-1}) / (1 + parRate_n).
     */
    static YieldCurve bootstrapAnnualParSwaps(const std::vector<int>& tenorYears,
                                              const std::vector<double>& parRates);

    /** Continuously-compounded zero rate (linear interpolation, flat extrapolation). */
    double zeroRate(double tenorYears) const;

    double discountFactor(double tenorYears) const;

    /** Implied continuously-compounded forward rate between two tenors. */
    double forwardRate(double fromYears, double toYears) const;

    /** Ascending pillar tenors (Java: navigableKeySet view; here a copy). */
    std::vector<double> tenors() const;

private:
    YieldCurve() = default;

    std::map<double, double> zeros_;  // tenorYears -> cc zero rate
};

}  // namespace quantfinlib
