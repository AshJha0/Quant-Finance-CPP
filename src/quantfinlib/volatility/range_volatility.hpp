// Port of com.quantfinlib.volatility.RangeVolatility (Java): range-based
// volatility estimators from OHLC bars — Parkinson, Garman-Klass,
// Rogers-Satchell and Yang-Zhang (exact k = 0.34/(1.34 + (m+1)/(m-1))).
// All methods return ANNUALIZED volatility:
//   sqrt(perPeriodVariance * periodsPerYear),
// with the annualization factor supplied by the caller.
//
// The Java BarSeries overloads are ported as std::vector<Bar> overloads
// (BarSeries itself has no C++ port; Bar does).
#pragma once

#include <vector>

#include "quantfinlib/data/bar.hpp"

namespace quantfinlib {

class RangeVolatility {
public:
    RangeVolatility() = delete;

    /** Parkinson estimator from highs/lows, annualized. */
    static double parkinson(const std::vector<double>& high,
                            const std::vector<double>& low, double periodsPerYear);

    /** Parkinson over a whole bar series. */
    static double parkinson(const std::vector<Bar>& bars, double periodsPerYear);

    /** Garman-Klass estimator, annualized. */
    static double garmanKlass(const std::vector<double>& open,
                              const std::vector<double>& high,
                              const std::vector<double>& low,
                              const std::vector<double>& close, double periodsPerYear);

    /** Garman-Klass over a whole bar series. */
    static double garmanKlass(const std::vector<Bar>& bars, double periodsPerYear);

    /** Rogers-Satchell (drift-independent) estimator, annualized. */
    static double rogersSatchell(const std::vector<double>& open,
                                 const std::vector<double>& high,
                                 const std::vector<double>& low,
                                 const std::vector<double>& close, double periodsPerYear);

    /** Rogers-Satchell over a whole bar series. */
    static double rogersSatchell(const std::vector<Bar>& bars, double periodsPerYear);

    /**
     * Yang-Zhang estimator, annualized. Uses bars 1..n-1 as the estimation
     * periods (bar 0 only supplies the prior close for the first overnight
     * return), so it needs at least 3 bars for the sample variances to exist.
     */
    static double yangZhang(const std::vector<double>& open,
                            const std::vector<double>& high,
                            const std::vector<double>& low,
                            const std::vector<double>& close, double periodsPerYear);

    /** Yang-Zhang over a whole bar series. */
    static double yangZhang(const std::vector<Bar>& bars, double periodsPerYear);
};

}  // namespace quantfinlib
