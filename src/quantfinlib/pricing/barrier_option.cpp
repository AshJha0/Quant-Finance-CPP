#include "quantfinlib/pricing/barrier_option.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace barrier_option {

namespace {

/**
 * The Reiner-Rubinstein knock-in value: the vanilla priced on the
 * barrier-reflected path measure. Sign symmetry handles the put mirror.
 */
double reflectionIn(OptionType type, double spot, double strike,
                    double barrier, double rate, double carry,
                    double vol, double t) {
    int s = sign(type);  // +1 call (down barrier), -1 put (up barrier)
    double lambda = (rate - carry + 0.5 * vol * vol) / (vol * vol);
    double sq = vol * std::sqrt(t);
    double y = std::log(barrier * barrier / (spot * strike)) / sq + lambda * sq;
    double hs = barrier / spot;
    return s * (spot * std::exp(-carry * t) * std::pow(hs, 2 * lambda)
                    * math_utils::normCdf(s * y)
            - strike * std::exp(-rate * t) * std::pow(hs, 2 * lambda - 2)
                    * math_utils::normCdf(s * (y - sq)));
}

void validateCommon(double spot, double strike, double barrier) {
    if (spot <= 0 || strike <= 0 || barrier <= 0) {
        throw std::invalid_argument("spot, strike, barrier must be > 0");
    }
}

void validateDownCall(double spot, double strike, double barrier) {
    validateCommon(spot, strike, barrier);
    if (barrier >= spot) {
        throw std::invalid_argument(
                "down barrier " + std::to_string(barrier)
                + " already breached at spot " + std::to_string(spot));
    }
    if (barrier > strike) {
        throw std::invalid_argument(
                "reverse barrier (H > K on a call) is not supported in closed form here — "
                "price it with Monte Carlo or a barrier-aware tree");
    }
}

void validateUpPut(double spot, double strike, double barrier) {
    validateCommon(spot, strike, barrier);
    if (barrier <= spot) {
        throw std::invalid_argument(
                "up barrier " + std::to_string(barrier)
                + " already breached at spot " + std::to_string(spot));
    }
    if (barrier < strike) {
        throw std::invalid_argument(
                "reverse barrier (H < K on a put) is not supported in closed form here — "
                "price it with Monte Carlo or a barrier-aware tree");
    }
}

}  // namespace

double downAndInCall(double spot, double strike, double barrier,
                     double rate, double carry, double vol, double timeYears) {
    validateDownCall(spot, strike, barrier);
    if (timeYears <= 0) {
        return 0;  // never touched, expires worthless as a knock-in
    }
    return reflectionIn(OptionType::CALL, spot, strike, barrier, rate, carry,
                        vol, timeYears);
}

double downAndOutCall(double spot, double strike, double barrier,
                      double rate, double carry, double vol, double timeYears) {
    validateDownCall(spot, strike, barrier);
    // In-out parity: holding both KI and KO replicates the vanilla.
    return black_scholes::price(OptionType::CALL, spot, strike, rate, carry,
                                vol, timeYears)
            - downAndInCall(spot, strike, barrier, rate, carry, vol, timeYears);
}

double upAndInPut(double spot, double strike, double barrier,
                  double rate, double carry, double vol, double timeYears) {
    validateUpPut(spot, strike, barrier);
    if (timeYears <= 0) {
        return 0;
    }
    return reflectionIn(OptionType::PUT, spot, strike, barrier, rate, carry,
                        vol, timeYears);
}

double upAndOutPut(double spot, double strike, double barrier,
                   double rate, double carry, double vol, double timeYears) {
    validateUpPut(spot, strike, barrier);
    return black_scholes::price(OptionType::PUT, spot, strike, rate, carry,
                                vol, timeYears)
            - upAndInPut(spot, strike, barrier, rate, carry, vol, timeYears);
}

}  // namespace barrier_option
}  // namespace quantfinlib
