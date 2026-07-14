#include "quantfinlib/pricing/variance_swap.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/volatility/volatility_index.hpp"

namespace quantfinlib {
namespace variance_swap {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}  // namespace

double fairVariance(const std::vector<double>& strikes,
                    const std::vector<double>& putMids,
                    const std::vector<double>& callMids,
                    double forward, double rate, double tYears) {
    double vol = VolatilityIndex::index(strikes, putMids, callMids, forward,
                                        rate, tYears);
    return vol * vol;
}

double volSwapStrike(double fairVariance, double varianceOfVariance) {
    if (!(fairVariance > 0) || fairVariance == kInf) {
        throw std::invalid_argument("fairVariance must be positive and finite");
    }
    if (!(varianceOfVariance >= 0) || varianceOfVariance == kInf) {
        throw std::invalid_argument("varianceOfVariance must be >= 0 and finite");
    }
    return std::sqrt(fairVariance)
            - varianceOfVariance / (8 * std::pow(fairVariance, 1.5));
}

double varianceNotional(double vegaNotional, double strikeVol) {
    if (!(strikeVol > 0) || strikeVol == kInf) {
        throw std::invalid_argument(
                "strikeVol must be positive and finite, got " + std::to_string(strikeVol));
    }
    if (!std::isfinite(vegaNotional)) {
        throw std::invalid_argument("vegaNotional must be finite");
    }
    return vegaNotional / (2 * strikeVol);
}

double markToMarket(double strikeVariance, double realizedVariance,
                    double remainingFair, double tElapsedYears,
                    double tTotalYears, double rate) {
    if (!(strikeVariance > 0) || strikeVariance == kInf) {
        throw std::invalid_argument("strikeVariance must be positive and finite");
    }
    if (!(realizedVariance >= 0) || realizedVariance == kInf
            || !(remainingFair >= 0) || remainingFair == kInf) {
        throw std::invalid_argument("variances must be >= 0 and finite");
    }
    if (!(tTotalYears > 0) || tTotalYears == kInf) {
        throw std::invalid_argument("tTotalYears must be positive and finite");
    }
    if (!(tElapsedYears >= 0) || tElapsedYears > tTotalYears) {
        throw std::invalid_argument(
                "tElapsedYears must be in [0, " + std::to_string(tTotalYears)
                + "], got " + std::to_string(tElapsedYears));
    }
    if (!std::isfinite(rate)) {
        throw std::invalid_argument("rate must be finite");
    }
    double weightElapsed = tElapsedYears / tTotalYears;
    double expected = weightElapsed * realizedVariance
            + (1 - weightElapsed) * remainingFair;
    return std::exp(-rate * (tTotalYears - tElapsedYears))
            * (expected - strikeVariance);
}

}  // namespace variance_swap
}  // namespace quantfinlib
