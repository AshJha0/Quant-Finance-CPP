#include "quantfinlib/pricing/black76.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace black76 {

namespace {

// ln(F/K) needs F > 0 and K >= 0. Negative rates belong in bachelier or a
// shifted model; silently returning NaN here is how they used to fail.
void requireLognormalDomain(double forward, double strike) {
    if (!(forward > 0) || !(strike >= 0)) {
        throw std::invalid_argument(
                "Black-76 needs forward > 0 and strike >= 0, got F=" + std::to_string(forward)
                + " K=" + std::to_string(strike) + "; use bachelier or a shifted model");
    }
}

}  // namespace

double price(OptionType type, double forward, double strike,
             double rate, double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0) {
        return std::exp(-rate * std::max(timeYears, 0.0))
                * black_scholes::intrinsic(type, forward, strike);
    }
    requireLognormalDomain(forward, strike);
    double df = std::exp(-rate * timeYears);
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(forward / strike) + 0.5 * vol * vol * timeYears)
            / (vol * sqrtT);
    double d2 = d1 - vol * sqrtT;
    if (type == OptionType::CALL) {
        return df * (forward * math_utils::normCdf(d1) - strike * math_utils::normCdf(d2));
    }
    return df * (strike * math_utils::normCdf(-d2) - forward * math_utils::normCdf(-d1));
}

double delta(OptionType type, double forward, double strike,
             double rate, double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0) {
        double intrinsicDelta = type == OptionType::CALL
                ? (forward > strike ? 1.0 : 0.0) : (forward < strike ? -1.0 : 0.0);
        return std::exp(-rate * std::max(timeYears, 0.0)) * intrinsicDelta;
    }
    requireLognormalDomain(forward, strike);
    double d1 = (std::log(forward / strike) + 0.5 * vol * vol * timeYears)
            / (vol * std::sqrt(timeYears));
    double df = std::exp(-rate * timeYears);
    return type == OptionType::CALL
            ? df * math_utils::normCdf(d1)
            : df * (math_utils::normCdf(d1) - 1);
}

double vega(double forward, double strike, double rate,
            double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0) {
        return 0;
    }
    requireLognormalDomain(forward, strike);
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(forward / strike) + 0.5 * vol * vol * timeYears)
            / (vol * sqrtT);
    return std::exp(-rate * timeYears) * forward * math_utils::normPdf(d1) * sqrtT;
}

double impliedVol(OptionType type, double marketPrice, double forward,
                  double strike, double rate, double timeYears) {
    double lo = 1e-6;
    double hi = 5.0;
    if (!(marketPrice > price(type, forward, strike, rate, lo, timeYears))
            || !(marketPrice < price(type, forward, strike, rate, hi, timeYears))) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    for (int i = 0; i < 100; i++) {
        double mid = 0.5 * (lo + hi);
        if (price(type, forward, strike, rate, mid, timeYears) < marketPrice) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

}  // namespace black76
}  // namespace quantfinlib
