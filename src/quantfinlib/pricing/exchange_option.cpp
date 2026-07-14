#include "quantfinlib/pricing/exchange_option.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace exchange_option {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

void requirePositive(double v, const char* name) {
    if (!(v > 0) || v == kInf) {
        throw std::invalid_argument(std::string(name)
                + " must be positive and finite, got " + std::to_string(v));
    }
}

void requireVols(double vol1, double vol2, double rho) {
    if (!(vol1 >= 0) || vol1 == kInf || !(vol2 >= 0) || vol2 == kInf) {
        throw std::invalid_argument("vols must be >= 0 and finite");
    }
    if (!(rho >= -1) || !(rho <= 1)) {
        throw std::invalid_argument(
                "rho must be in [-1, 1], got " + std::to_string(rho));
    }
}

}  // namespace

double margrabe(double s1, double s2, double q1, double q2,
                double vol1, double vol2, double rho, double timeYears) {
    requirePositive(s1, "s1");
    requirePositive(s2, "s2");
    requireVols(vol1, vol2, rho);
    if (!std::isfinite(q1) || !std::isfinite(q2)) {
        throw std::invalid_argument("yields must be finite");
    }
    if (timeYears <= 0) {
        return std::max(0.0, s1 - s2);
    }
    double variance = vol1 * vol1 + vol2 * vol2 - 2 * rho * vol1 * vol2;
    double f1 = s1 * std::exp(-q1 * timeYears);
    double f2 = s2 * std::exp(-q2 * timeYears);
    if (variance <= 0) {
        // Perfectly correlated equal-vol legs: the ratio cannot move.
        return std::max(0.0, f1 - f2);
    }
    double sigma = std::sqrt(variance);
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(s1 / s2) + (q2 - q1 + 0.5 * variance) * timeYears)
            / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;
    return f1 * math_utils::normCdf(d1) - f2 * math_utils::normCdf(d2);
}

double kirkSpreadCall(double f1, double f2, double strike, double rate,
                      double vol1, double vol2, double rho, double timeYears) {
    requirePositive(f1, "f1");
    if (!(f2 >= 0) || f2 == kInf) {
        throw std::invalid_argument(
                "f2 must be >= 0 and finite, got " + std::to_string(f2));
    }
    if (!(strike >= 0) || strike == kInf) {
        throw std::invalid_argument(
                "strike must be >= 0 and finite, got " + std::to_string(strike));
    }
    if (f2 + strike <= 0) {
        throw std::invalid_argument("f2 + strike must be > 0");
    }
    if (!std::isfinite(rate)) {
        throw std::invalid_argument("rate must be finite");
    }
    requireVols(vol1, vol2, rho);
    double df = std::exp(-rate * std::max(timeYears, 0.0));
    if (timeYears <= 0) {
        return df * std::max(0.0, f1 - f2 - strike);
    }
    double f = f2 / (f2 + strike);
    double variance = vol1 * vol1 - 2 * rho * vol1 * vol2 * f + vol2 * vol2 * f * f;
    if (variance <= 0) {
        return df * std::max(0.0, f1 - f2 - strike);
    }
    double sigma = std::sqrt(variance);
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(f1 / (f2 + strike)) + 0.5 * variance * timeYears)
            / (sigma * sqrtT);
    double d2 = d1 - sigma * sqrtT;
    return df * (f1 * math_utils::normCdf(d1)
            - (f2 + strike) * math_utils::normCdf(d2));
}

}  // namespace exchange_option
}  // namespace quantfinlib
