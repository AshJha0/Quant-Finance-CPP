#include "quantfinlib/pricing/bachelier.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace bachelier {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
const double kSqrt2Pi = std::sqrt(2 * 3.141592653589793);

void validate(double forward, double strike, double rate, double normalVol, double timeYears) {
    if (!std::isfinite(forward) || !std::isfinite(strike) || !std::isfinite(rate)
            || !(normalVol >= 0) || normalVol == kInf || std::isnan(timeYears)
            || timeYears == kInf) {
        throw std::invalid_argument(
                "forward, strike, rate, time must be finite and normal vol >= 0");
    }
}

// h(u) = n(u) - u*N(-u), through the Mills ratio for large u.
double hOf(double u) {
    double phi = math_utils::normPdf(u);
    if (u < 2) {
        return phi - u * math_utils::normCdf(-u);
    }
    return phi * (1 - u * (math_utils::normCdf(-u) / phi));
}

}  // namespace

double price(OptionType type, double forward, double strike,
             double rate, double normalVol, double timeYears) {
    validate(forward, strike, rate, normalVol, timeYears);
    double df = std::exp(-rate * std::max(timeYears, 0.0));
    int w = sign(type);
    if (timeYears <= 0 || normalVol <= 0) {
        return df * std::max(w * (forward - strike), 0.0);
    }
    double sd = normalVol * std::sqrt(timeYears);
    double d = (forward - strike) / sd;
    return df * (w * (forward - strike) * math_utils::normCdf(w * d)
                 + sd * math_utils::normPdf(d));
}

double delta(OptionType type, double forward, double strike,
             double rate, double normalVol, double timeYears) {
    validate(forward, strike, rate, normalVol, timeYears);
    double df = std::exp(-rate * std::max(timeYears, 0.0));
    int w = sign(type);
    if (timeYears <= 0 || normalVol <= 0) {
        double m = w * (forward - strike);
        return df * w * (m > 0 ? 1 : m == 0 ? 0.5 : 0);
    }
    double d = (forward - strike) / (normalVol * std::sqrt(timeYears));
    return df * w * math_utils::normCdf(w * d);
}

double vega(double forward, double strike, double rate,
            double normalVol, double timeYears) {
    validate(forward, strike, rate, normalVol, timeYears);
    if (timeYears <= 0) {
        return 0;
    }
    double df = std::exp(-rate * timeYears);
    if (normalVol <= 0) {
        return forward == strike ? df * std::sqrt(timeYears) / kSqrt2Pi : 0;
    }
    double d = (forward - strike) / (normalVol * std::sqrt(timeYears));
    return df * std::sqrt(timeYears) * math_utils::normPdf(d);
}

double impliedVol(OptionType type, double price, double forward,
                  double strike, double rate, double timeYears) {
    if (!(timeYears > 0) || timeYears == kInf || !std::isfinite(price)
            || !std::isfinite(forward) || !std::isfinite(strike) || !std::isfinite(rate)) {
        return kNaN;
    }
    double undiscounted = price * std::exp(rate * timeYears);
    double intrinsic = std::max(sign(type) * (forward - strike), 0.0);
    double v = undiscounted - intrinsic;  // time value = OTM option value by parity
    double sqrtT = std::sqrt(timeYears);
    if (v < 0) {
        return kNaN;
    }
    if (v == 0) {
        return 0.0;
    }
    double a = std::fabs(forward - strike);
    if (a == 0) {
        return v * kSqrt2Pi / sqrtT;
    }
    double target = std::log(v / a);
    // Seed: near the money h(u) ~ n(0) so u ~ n(0)/g; far out h ~ n(u)/u^2.
    double g = v / a;
    double u = g > 0.2 ? 1 / (kSqrt2Pi * g)
                       : std::sqrt(std::max(-2 * std::log(g * kSqrt2Pi), 1.0));
    double lo = 0, hi = kInf;
    for (int i = 0; i < 100; i++) {
        double h = hOf(u);
        double f = std::log(h / u) - target;  // decreasing in u
        if (!(h > 0)) {
            f = -kInf;  // h underflowed: u is far too large
        }
        if (f > 0) {
            lo = u;
        } else if (f < 0) {
            hi = u;
        } else {
            break;
        }
        double slope = -math_utils::normCdf(-u) / h - 1 / u;
        double next = u - f / slope;
        if (!(next > lo && next < hi)) {
            next = std::isinf(hi) ? 2 * u + 1 : 0.5 * (lo + hi);
        }
        double ulp = std::nextafter(u, kInf) - u;
        if (std::fabs(next - u) <= 4 * ulp) {
            u = next;
            break;
        }
        u = next;
    }
    return a / (u * sqrtT);
}

}  // namespace bachelier
}  // namespace quantfinlib
