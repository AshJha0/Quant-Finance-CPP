#include "quantfinlib/pricing/structured_notes.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace structured_notes {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

void requireCommon(double par, double spot, double rate, double carry,
                   double vol, double timeYears) {
    if (!(par > 0) || par == kInf) {
        throw std::invalid_argument("par must be positive and finite");
    }
    if (!(spot > 0) || spot == kInf) {
        throw std::invalid_argument("spot must be positive and finite");
    }
    if (!std::isfinite(rate) || !std::isfinite(carry)) {
        throw std::invalid_argument("rate and carry must be finite");
    }
    if (!(vol >= 0) || vol == kInf) {
        throw std::invalid_argument("vol must be >= 0 and finite");
    }
    if (!(timeYears > 0) || timeYears == kInf) {
        throw std::invalid_argument("timeYears must be positive and finite");
    }
}

}  // namespace

double reverseConvertible(double par, double couponRate,
                          double spot, double strike, double rate,
                          double carry, double vol, double timeYears) {
    requireCommon(par, spot, rate, carry, vol, timeYears);
    if (!(strike > 0) || strike == kInf) {
        throw std::invalid_argument("strike must be positive and finite");
    }
    if (!(couponRate >= 0) || couponRate == kInf) {
        throw std::invalid_argument("couponRate must be >= 0 and finite");
    }
    double df = std::exp(-rate * timeYears);
    double put = black_scholes::price(OptionType::PUT, spot, strike, rate,
                                      carry, vol, timeYears);
    return par * (1 + couponRate) * df - (par / strike) * put;
}

double reverseConvertibleDelta(double par, double spot, double strike,
                               double rate, double carry, double vol,
                               double timeYears) {
    requireCommon(par, spot, rate, carry, vol, timeYears);
    return -(par / strike)
            * black_scholes::delta(OptionType::PUT, spot, strike, rate, carry,
                                   vol, timeYears);
}

double capitalProtectedNote(double par, double protection, double participation,
                            double spot, double rate, double carry,
                            double vol, double timeYears) {
    requireCommon(par, spot, rate, carry, vol, timeYears);
    if (!(protection > 0) || protection > 1) {
        throw std::invalid_argument(
                "protection must be in (0, 1], got " + std::to_string(protection));
    }
    if (!(participation >= 0) || participation == kInf) {
        throw std::invalid_argument("participation must be >= 0 and finite");
    }
    double df = std::exp(-rate * timeYears);
    double call = black_scholes::price(OptionType::CALL, spot, spot, rate,
                                       carry, vol, timeYears);
    return protection * par * df + participation * (par / spot) * call;
}

double participationFor(double par, double protection, double issuePrice,
                        double spot, double rate, double carry,
                        double vol, double timeYears) {
    requireCommon(par, spot, rate, carry, vol, timeYears);
    if (!(issuePrice > 0) || issuePrice == kInf) {
        throw std::invalid_argument("issuePrice must be positive and finite");
    }
    if (!(protection > 0) || protection > 1) {
        throw std::invalid_argument(
                "protection must be in (0, 1], got " + std::to_string(protection));
    }
    double df = std::exp(-rate * timeYears);
    double floor = protection * par * df;
    if (issuePrice <= floor) {
        throw std::invalid_argument(
                "issue price " + std::to_string(issuePrice)
                + " does not even cover the protected floor " + std::to_string(floor)
                + ": no participation is affordable");
    }
    double call = black_scholes::price(OptionType::CALL, spot, spot, rate,
                                       carry, vol, timeYears);
    if (!(call > 0)) {
        throw std::invalid_argument(
                "ATM call is worthless under these parameters: participation undefined");
    }
    return (issuePrice - floor) / ((par / spot) * call);
}

double discountCertificate(double spot, double cap, double rate,
                           double carry, double vol, double timeYears) {
    requireCommon(1, spot, rate, carry, vol, timeYears);
    if (!(cap > 0) || cap == kInf) {
        throw std::invalid_argument("cap must be positive and finite");
    }
    return spot * std::exp(-carry * timeYears)
            - black_scholes::price(OptionType::CALL, spot, cap, rate, carry,
                                   vol, timeYears);
}

double discountCertificateDelta(double spot, double cap, double rate,
                                double carry, double vol, double timeYears) {
    requireCommon(1, spot, rate, carry, vol, timeYears);
    return std::exp(-carry * timeYears)
            - black_scholes::delta(OptionType::CALL, spot, cap, rate, carry,
                                   vol, timeYears);
}

}  // namespace structured_notes
}  // namespace quantfinlib
