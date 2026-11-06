#include "quantfinlib/rates/rates_options.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/pricing/bachelier.hpp"
#include "quantfinlib/pricing/black76.hpp"

namespace quantfinlib {
namespace rates_options {

namespace {

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

int periods(double startYears, double tenorYears, int paymentsPerYear) {
    if (paymentsPerYear < 1 || 12 % paymentsPerYear != 0) {
        throw std::invalid_argument("paymentsPerYear must divide 12");
    }
    if (!(startYears >= 0) || !(tenorYears > 0) || !std::isfinite(startYears + tenorYears)) {
        throw std::invalid_argument("need start >= 0 and tenor > 0");
    }
    double n = tenorYears * paymentsPerYear;
    double rounded = std::nearbyint(n);
    if (std::fabs(n - rounded) > 1e-9 || rounded < 1) {
        throw std::invalid_argument("tenor must be a whole number of periods");
    }
    return static_cast<int>(rounded);
}

void requireFiniteStrike(double strike) {
    if (!std::isfinite(strike)) {
        throw std::invalid_argument("strike must be finite, got " + std::to_string(strike));
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
        pv += dfPay * black76::price(type, simpleForward, strike, 0, vol, i - 1);
    }
    return pv;
}

}  // namespace

VolModel VolModel::shiftedLognormal(double shift) {
    if (!std::isfinite(shift)) {
        throw std::invalid_argument("shift must be finite, got " + std::to_string(shift));
    }
    return VolModel(Type::SHIFTED_LOGNORMAL, shift);
}

double VolModel::forwardValue(OptionType type, double forward, double strike, double vol,
                              double expiry) const {
    if (!(vol >= 0) || vol == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("vol must be >= 0 and finite, got " + std::to_string(vol));
    }
    if (type_ == Type::NORMAL) {
        return bachelier::price(type, forward, strike, 0, vol, expiry);
    }
    if (expiry <= 0) {
        return black_scholes::intrinsic(type, forward, strike);
    }
    double f = forward + shift_;
    double k = strike + shift_;
    if (!(f > 0) || !(k > 0)) {
        throw std::invalid_argument("lognormal model needs forward + shift > 0 and strike + "
                                    "shift > 0, got F=" + std::to_string(forward) + " K="
                                    + std::to_string(strike) + " shift="
                                    + std::to_string(shift_));
    }
    return black76::price(type, f, k, 0, vol, expiry);
}

double VolModel::impliedVol(OptionType type, double forwardValue, double forward, double strike,
                            double expiry) const {
    if (type_ == Type::NORMAL) {
        return bachelier::impliedVol(type, forwardValue, forward, strike, 0, expiry);
    }
    return black76::impliedVol(type, forwardValue, forward + shift_, strike + shift_, 0, expiry);
}

double annuity(const YieldCurve& curve, double startYears, double tenorYears,
               int paymentsPerYear) {
    int n = periods(startYears, tenorYears, paymentsPerYear);
    double tau = 1.0 / paymentsPerYear;
    double a = 0;
    for (int i = 1; i <= n; i++) {
        a += tau * curve.discountFactor(startYears + i * tau);
    }
    return a;
}

double forwardSwapRate(const YieldCurve& curve, double startYears, double tenorYears,
                       int paymentsPerYear) {
    double a = annuity(curve, startYears, tenorYears, paymentsPerYear);
    return (curve.discountFactor(startYears) - curve.discountFactor(startYears + tenorYears)) / a;
}

double swaption(const YieldCurve& curve, double startYears, double tenorYears,
                int paymentsPerYear, double strike, double vol, bool payer,
                const VolModel& model) {
    requireFiniteStrike(strike);
    double fsr = forwardSwapRate(curve, startYears, tenorYears, paymentsPerYear);
    double a = annuity(curve, startYears, tenorYears, paymentsPerYear);
    return a * model.forwardValue(payer ? OptionType::CALL : OptionType::PUT, fsr, strike, vol,
                                  startYears);
}

double swaptionImpliedVol(const YieldCurve& curve, double startYears, double tenorYears,
                          int paymentsPerYear, double strike, double price, bool payer,
                          const VolModel& model) {
    double fsr = forwardSwapRate(curve, startYears, tenorYears, paymentsPerYear);
    double a = annuity(curve, startYears, tenorYears, paymentsPerYear);
    return model.impliedVol(payer ? OptionType::CALL : OptionType::PUT, price / a, fsr, strike,
                            startYears);
}

double capFloor(const YieldCurve& curve, double maturityYears, int paymentsPerYear,
                double strike, double vol, bool isCap, const VolModel& model) {
    int n = periods(0, maturityYears, paymentsPerYear);
    requireFiniteStrike(strike);
    double tau = 1.0 / paymentsPerYear;
    double pv = 0;
    for (int i = 1; i <= n; i++) {
        double dfPay = curve.discountFactor(i * tau);
        double fwd = (curve.discountFactor((i - 1) * tau) / dfPay - 1) / tau;
        pv += tau * dfPay * model.forwardValue(isCap ? OptionType::CALL : OptionType::PUT, fwd,
                                               strike, vol, (i - 1) * tau);
    }
    return pv;
}

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
    return a * black76::price(payer ? OptionType::CALL : OptionType::PUT,
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
