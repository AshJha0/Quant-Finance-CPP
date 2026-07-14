#include "quantfinlib/pricing/quanto_option.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace quanto_option {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

void validate(double spot, double assetVol, double fxVol, double rho,
              double domesticRate, double assetYield) {
    if (!(spot > 0) || spot == kInf) {
        throw std::invalid_argument(
                "spot must be positive and finite, got " + std::to_string(spot));
    }
    if (!(assetVol >= 0) || assetVol == kInf || !(fxVol >= 0) || fxVol == kInf) {
        throw std::invalid_argument("vols must be >= 0 and finite");
    }
    if (!(rho >= -1) || !(rho <= 1)) {
        throw std::invalid_argument(
                "rho must be in [-1, 1], got " + std::to_string(rho));
    }
    if (!std::isfinite(domesticRate) || !std::isfinite(assetYield)) {
        throw std::invalid_argument("rates must be finite");
    }
}

}  // namespace

double quantoForward(double spot, double domesticRate, double assetYield,
                     double assetVol, double fxVol, double rho, double timeYears) {
    validate(spot, assetVol, fxVol, rho, domesticRate, assetYield);
    if (!(timeYears >= 0) || timeYears == kInf) {
        throw std::invalid_argument("timeYears must be >= 0 and finite");
    }
    return spot * std::exp(
            (domesticRate - assetYield - rho * assetVol * fxVol) * timeYears);
}

double price(OptionType type, double spot, double strike,
             double domesticRate, double assetYield,
             double assetVol, double fxVol, double rho, double timeYears) {
    validate(spot, assetVol, fxVol, rho, domesticRate, assetYield);
    if (!(strike > 0) || strike == kInf) {
        throw std::invalid_argument(
                "strike must be positive and finite, got " + std::to_string(strike));
    }
    return black_scholes::price(type, spot, strike, domesticRate,
                                assetYield + rho * assetVol * fxVol, assetVol,
                                timeYears);
}

}  // namespace quanto_option
}  // namespace quantfinlib
