// Port of com.quantfinlib.screener.Fundamentals (Java record; placed under
// data/ here since this port has no screener/ directory). Use NaN for
// unknown fields; fundamental filters never match on NaN values.
#pragma once

#include <limits>

namespace quantfinlib {

struct Fundamentals {
    double marketCap = std::numeric_limits<double>::quiet_NaN();
    double peRatio = std::numeric_limits<double>::quiet_NaN();
    double pbRatio = std::numeric_limits<double>::quiet_NaN();
    double eps = std::numeric_limits<double>::quiet_NaN();
    double roe = std::numeric_limits<double>::quiet_NaN();
    double dividendYield = std::numeric_limits<double>::quiet_NaN();
    double debtToEquity = std::numeric_limits<double>::quiet_NaN();

    static Fundamentals unknown() { return Fundamentals{}; }
};

}  // namespace quantfinlib
