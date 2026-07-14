#include "quantfinlib/rates/rates_options.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace rates_options {

namespace {

enum class OptionType { CALL, PUT };

// File-local copy of com.quantfinlib.pricing.Black76.price (the pricing
// domain is outside this port's scope; formula and branches copied 1:1):
//   call = df * [F*Phi(d1) - K*Phi(d2)],
//   d1 = (ln(F/K) + sigma^2 T / 2) / (sigma sqrt(T)).
// timeYears <= 0 or vol <= 0 collapses to discounted intrinsic.
double black76Price(OptionType type, double forward, double strike,
                    double rate, double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0) {
        double intrinsic = type == OptionType::CALL ? forward - strike
                                                    : strike - forward;
        if (intrinsic < 0) {
            intrinsic = 0;
        }
        return std::exp(-rate * std::max(timeYears, 0.0)) * intrinsic;
    }
    double df = std::exp(-rate * timeYears);
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(forward / strike) + 0.5 * vol * vol * timeYears)
            / (vol * sqrtT);
    double d2 = d1 - vol * sqrtT;
    if (type == OptionType::CALL) {
        return df * (forward * math_utils::normCdf(d1)
                     - strike * math_utils::normCdf(d2));
    }
    return df * (strike * math_utils::normCdf(-d2)
                 - forward * math_utils::normCdf(-d1));
}

void validate(int startYears, int tenorYears) {
    if (startYears < 0 || tenorYears < 1) {
        throw std::invalid_argument("need startYears >= 0 and tenorYears >= 1: "
                + std::to_string(startYears) + "/" + std::to_string(tenorYears));
    }
}

void requirePositiveVolAndStrike(double strike, double vol) {
    if (!(strike > 0) || strike == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("strike must be positive and finite, got "
                                    + std::to_string(strike));
    }
    if (!(vol > 0) || vol == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("vol must be positive and finite, got "
                                    + std::to_string(vol));
    }
}

double capletStrip(const YieldCurve& curve, int maturityYears,
                   double strike, double vol, OptionType type) {
    if (maturityYears < 1) {
        throw std::invalid_argument("maturityYears must be >= 1, got "
                                    + std::to_string(maturityYears));
    }
    requirePositiveVolAndStrike(strike, vol);
    double pv = 0;
    for (int i = 1; i <= maturityYears; i++) {
        double dfPay = curve.discountFactor(i);
        double simpleForward = curve.discountFactor(i - 1) / dfPay - 1;
        // Fixes at i-1 (the first period is already fixed: T=0 ->
        // Black76 returns discounted intrinsic, i.e. pure intrinsic
        // here since its own rate argument is 0), pays at i.
        pv += dfPay * black76Price(type, simpleForward, strike, 0, vol, i - 1);
    }
    return pv;
}

}  // namespace

double annuity(const YieldCurve& curve, int startYears, int tenorYears) {
    validate(startYears, tenorYears);
    double a = 0;
    for (int i = 1; i <= tenorYears; i++) {
        a += curve.discountFactor(startYears + i);
    }
    return a;
}

double forwardSwapRate(const YieldCurve& curve, int startYears, int tenorYears) {
    validate(startYears, tenorYears);
    double dfStart = curve.discountFactor(startYears);
    double dfEnd = curve.discountFactor(startYears + tenorYears);
    return (dfStart - dfEnd) / annuity(curve, startYears, tenorYears);
}

double swaption(const YieldCurve& curve, int startYears, int tenorYears,
                double strike, double vol, bool payer) {
    requirePositiveVolAndStrike(strike, vol);
    double fsr = forwardSwapRate(curve, startYears, tenorYears);
    double a = annuity(curve, startYears, tenorYears);
    return a * black76Price(payer ? OptionType::CALL : OptionType::PUT,
                            fsr, strike, 0, vol, startYears);
}

double cap(const YieldCurve& curve, int maturityYears, double strike, double vol) {
    return capletStrip(curve, maturityYears, strike, vol, OptionType::CALL);
}

double floor(const YieldCurve& curve, int maturityYears, double strike, double vol) {
    return capletStrip(curve, maturityYears, strike, vol, OptionType::PUT);
}

}  // namespace rates_options
}  // namespace quantfinlib
