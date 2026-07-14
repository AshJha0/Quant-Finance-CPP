#include "quantfinlib/pricing/asian_option.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace asian_option {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

/** Discounted Black-style price on a lognormal with mean f, log-variance v2. */
double blackOnLognormal(OptionType type, double f, double k,
                        double v2, double rate, double timeYears) {
    double df = std::exp(-rate * timeYears);
    if (v2 <= 0) {
        // Zero-vol intrinsic on the forward.
        return df * std::max(0.0, sign(type) * (f - k));
    }
    double sd = std::sqrt(v2);
    double d1 = (std::log(f / k) + 0.5 * v2) / sd;
    double d2 = d1 - sd;
    int s = sign(type);
    return df * s * (f * math_utils::normCdf(s * d1) - k * math_utils::normCdf(s * d2));
}

void validate(double spot, double strike, double rate, double carry,
              double vol, double timeYears, int averagingPoints) {
    if (!(spot > 0) || spot == kInf) {
        throw std::invalid_argument(
                "spot must be positive and finite, got " + std::to_string(spot));
    }
    if (!(strike > 0) || strike == kInf) {
        throw std::invalid_argument(
                "strike must be positive and finite, got " + std::to_string(strike));
    }
    if (!std::isfinite(rate) || !std::isfinite(carry)) {
        throw std::invalid_argument("rate and carry must be finite");
    }
    if (!(vol >= 0) || vol == kInf) {
        throw std::invalid_argument(
                "vol must be >= 0 and finite, got " + std::to_string(vol));
    }
    if (!(timeYears > 0) || timeYears == kInf) {
        throw std::invalid_argument(
                "timeYears must be positive and finite, got " + std::to_string(timeYears));
    }
    if (averagingPoints < 1) {
        throw std::invalid_argument(
                "averagingPoints must be >= 1, got " + std::to_string(averagingPoints));
    }
}

}  // namespace

double geometricPrice(OptionType type, double spot, double strike,
                      double rate, double carry, double vol, double timeYears,
                      int averagingPoints) {
    validate(spot, strike, rate, carry, vol, timeYears, averagingPoints);
    double n = averagingPoints;
    double meanLog = std::log(spot)
            + (rate - carry - 0.5 * vol * vol) * timeYears * (n + 1) / (2 * n);
    double varLog = vol * vol * timeYears * (n + 1) * (2 * n + 1) / (6 * n * n);
    double forward = std::exp(meanLog + 0.5 * varLog);
    return blackOnLognormal(type, forward, strike, varLog, rate, timeYears);
}

double arithmeticPrice(OptionType type, double spot, double strike,
                       double rate, double carry, double vol, double timeYears,
                       int averagingPoints) {
    validate(spot, strike, rate, carry, vol, timeYears, averagingPoints);
    int n = averagingPoints;
    double h = timeYears / n;
    double g = rate - carry;
    double m1 = 0;
    for (int i = 1; i <= n; i++) {
        m1 += std::exp(g * i * h);
    }
    m1 *= spot / n;
    double m2 = 0;
    for (int i = 1; i <= n; i++) {
        for (int j = 1; j <= n; j++) {
            m2 += std::exp(g * (i + j) * h + vol * vol * std::min(i, j) * h);
        }
    }
    m2 *= spot * spot / (static_cast<double>(n) * n);
    // Matched log-variance; guard the deterministic edge (vol = 0 makes
    // M2 exactly M1^2 and the log a rounding-noise 0/0).
    double varLog = m2 > m1 * m1 ? std::log(m2 / (m1 * m1)) : 0;
    return blackOnLognormal(type, m1, strike, varLog, rate, timeYears);
}

}  // namespace asian_option
}  // namespace quantfinlib
