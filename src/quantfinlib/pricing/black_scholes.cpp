#include "quantfinlib/pricing/black_scholes.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace black_scholes {

namespace {

double intrinsicDelta(OptionType type, double spot, double strike) {
    if (type == OptionType::CALL) {
        return spot > strike ? 1 : 0;
    }
    return spot < strike ? -1 : 0;
}

double d1(double spot, double strike, double rate, double carry,
          double vol, double timeYears) {
    return (std::log(spot / strike) + (rate - carry + 0.5 * vol * vol) * timeYears)
            / (vol * std::sqrt(timeYears));
}

}  // namespace

double price(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return intrinsic(type, spot, strike);
    }
    if (vol <= 0) {
        // Deterministic world: discounted forward intrinsic. Without this
        // branch the ATM-forward case is 0/0 in d1 and the price comes back
        // NaN (off-ATM it survives only because +/-Inf happens to hit the
        // right CDF tail).
        return std::max(0.0, sign(type) * (spot * std::exp(-carry * timeYears)
                - strike * std::exp(-rate * timeYears)));
    }
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    double d2 = d1v - vol * std::sqrt(timeYears);
    double df = std::exp(-rate * timeYears);
    double cf = std::exp(-carry * timeYears);
    if (type == OptionType::CALL) {
        return spot * cf * math_utils::normCdf(d1v) - strike * df * math_utils::normCdf(d2);
    }
    return strike * df * math_utils::normCdf(-d2) - spot * cf * math_utils::normCdf(-d1v);
}

double delta(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return intrinsicDelta(type, spot, strike);
    }
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    double cf = std::exp(-carry * timeYears);
    return type == OptionType::CALL
            ? cf * math_utils::normCdf(d1v)
            : cf * (math_utils::normCdf(d1v) - 1);
}

double gamma(double spot, double strike, double rate, double carry,
             double vol, double timeYears) {
    if (timeYears <= 0) {
        return 0;
    }
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    return std::exp(-carry * timeYears) * math_utils::normPdf(d1v)
            / (spot * vol * std::sqrt(timeYears));
}

double vega(double spot, double strike, double rate, double carry,
            double vol, double timeYears) {
    if (timeYears <= 0) {
        return 0;
    }
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    return spot * std::exp(-carry * timeYears) * math_utils::normPdf(d1v)
            * std::sqrt(timeYears);
}

double theta(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return 0;
    }
    double sqrtT = std::sqrt(timeYears);
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    double d2 = d1v - vol * sqrtT;
    double cf = std::exp(-carry * timeYears);
    double df = std::exp(-rate * timeYears);
    double common = -spot * cf * math_utils::normPdf(d1v) * vol / (2 * sqrtT);
    if (type == OptionType::CALL) {
        return common - rate * strike * df * math_utils::normCdf(d2)
                + carry * spot * cf * math_utils::normCdf(d1v);
    }
    return common + rate * strike * df * math_utils::normCdf(-d2)
            - carry * spot * cf * math_utils::normCdf(-d1v);
}

double rho(OptionType type, double spot, double strike,
           double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return 0;
    }
    double d2 = d1(spot, strike, rate, carry, vol, timeYears) - vol * std::sqrt(timeYears);
    double df = std::exp(-rate * timeYears);
    return type == OptionType::CALL
            ? strike * timeYears * df * math_utils::normCdf(d2)
            : -strike * timeYears * df * math_utils::normCdf(-d2);
}

Greeks greeks(OptionType type, double spot, double strike,
              double rate, double carry, double vol, double timeYears) {
    return Greeks{
            price(type, spot, strike, rate, carry, vol, timeYears),
            delta(type, spot, strike, rate, carry, vol, timeYears),
            gamma(spot, strike, rate, carry, vol, timeYears),
            vega(spot, strike, rate, carry, vol, timeYears),
            theta(type, spot, strike, rate, carry, vol, timeYears),
            rho(type, spot, strike, rate, carry, vol, timeYears)};
}

double impliedVol(OptionType type, double marketPrice, double spot,
                  double strike, double rate, double carry, double timeYears) {
    double lo = 1e-4;
    double hi = 5.0;
    if (marketPrice < price(type, spot, strike, rate, carry, lo, timeYears)
            || marketPrice > price(type, spot, strike, rate, carry, hi, timeYears)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2;
        if (price(type, spot, strike, rate, carry, mid, timeYears) < marketPrice) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return (lo + hi) / 2;
}

double intrinsic(OptionType type, double spot, double strike) {
    return std::max(0.0, sign(type) * (spot - strike));
}

}  // namespace black_scholes
}  // namespace quantfinlib
