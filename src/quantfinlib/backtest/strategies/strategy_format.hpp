// Shared helper for strategy name() strings: formats a double the way
// Java string concatenation does for the values that appear in strategy
// parameters (integral doubles print with a trailing ".0", e.g. 30.0).
#pragma once

#include <cmath>
#include <cstdio>
#include <string>

namespace quantfinlib {
namespace strategies_detail {

inline std::string javaDouble(double v) {
    if (v == std::floor(v) && std::fabs(v) < 1e7) {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%.1f", v);
        return std::string(buf);
    }
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%g", v);
    return std::string(buf);
}

}  // namespace strategies_detail
}  // namespace quantfinlib
