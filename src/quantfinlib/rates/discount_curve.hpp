// Port of com.quantfinlib.rates.DiscountCurve (Java): a dated discount curve
// on pillar times (ACT/365 from the valuation date), storing y = -ln DF.
// LOG_LINEAR_DISCOUNT is piecewise-flat forwards. MONOTONE_CONVEX is the
// Hagan-West (2006) forward interpolation: it reprices the pillars, keeps the
// forwards continuous, and uses the four shape zones of the paper.
// Beyond the last pillar the last forward extends flat.
#pragma once

#include <vector>

#include "quantfinlib/rates/day_count.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

enum class CurveInterpolation { LOG_LINEAR_DISCOUNT, MONOTONE_CONVEX };

class DiscountCurve {
public:
    /** Times strictly increasing and positive, discount factors positive and finite. */
    static DiscountCurve of(const LocalDate& valuationDate, const std::vector<double>& times,
                            const std::vector<double>& discountFactors,
                            CurveInterpolation interpolation);

    /** As of, from y = -ln DF; negative rates make y negative, which is legal. */
    static DiscountCurve ofMinusLogDf(const LocalDate& valuationDate,
                                      const std::vector<double>& times,
                                      const std::vector<double>& minusLogDf,
                                      CurveInterpolation interpolation);

    double time(const LocalDate& date) const;
    const LocalDate& valuationDate() const { return valuationDate_; }
    int pillarCount() const { return static_cast<int>(t_.size()) - 1; }
    double pillarTime(int i) const { return t_.at(i + 1); }

    double discountFactor(double time) const;
    double discountFactor(const LocalDate& date) const;
    double zeroRate(double time) const;
    /** Simple forward over [start, end] with the instrument's day count. */
    double forwardRate(const LocalDate& start, const LocalDate& end, DayCount dayCount) const;
    double instantaneousForward(double time) const;
    double minusLogDf(double time) const;
    DiscountCurve parallelShift(double shift) const;

private:
    DiscountCurve(const LocalDate& valuationDate, CurveInterpolation interpolation,
                  const std::vector<double>& times, const std::vector<double>& minusLogDf);

    int interval(double time) const;

    LocalDate valuationDate_;
    CurveInterpolation interpolation_;
    std::vector<double> t_, y_, fd_, fn_;  // index 0 is time 0
};

}  // namespace quantfinlib
