#include "quantfinlib/pricing/higher_order_greeks.hpp"

#include <algorithm>
#include <cmath>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace higher_order_greeks {

namespace {

double d1(double spot, double strike, double rate, double carry,
          double vol, double timeYears) {
    return (std::log(spot / strike) + (rate - carry + 0.5 * vol * vol) * timeYears)
            / (vol * std::sqrt(timeYears));
}

}  // namespace

double vanna(double spot, double strike, double rate, double carry,
             double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0 || spot <= 0) {
        return 0;
    }
    double sqrtT = std::sqrt(timeYears);
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    double d2 = d1v - vol * sqrtT;
    return -std::exp(-carry * timeYears) * math_utils::normPdf(d1v) * d2 / vol;
}

double volga(double spot, double strike, double rate, double carry,
             double vol, double timeYears) {
    if (timeYears <= 0 || vol <= 0 || spot <= 0) {
        return 0;
    }
    double sqrtT = std::sqrt(timeYears);
    double d1v = d1(spot, strike, rate, carry, vol, timeYears);
    double d2 = d1v - vol * sqrtT;
    double vega = spot * std::exp(-carry * timeYears)
            * math_utils::normPdf(d1v) * sqrtT;
    return vega * d1v * d2 / vol;
}

double exchangeCrossGamma(double spot1, double spot2, double vol1,
                          double vol2, double correlation, double timeYears) {
    if (timeYears <= 0 || spot1 <= 0 || spot2 <= 0) {
        return 0;
    }
    double sigmaHat = std::sqrt(std::max(1e-12,
            vol1 * vol1 + vol2 * vol2 - 2 * correlation * vol1 * vol2));
    double sqrtT = std::sqrt(timeYears);
    double d1 = (std::log(spot1 / spot2) + 0.5 * sigmaHat * sigmaHat * timeYears)
            / (sigmaHat * sqrtT);
    return -math_utils::normPdf(d1) / (spot2 * sigmaHat * sqrtT);
}

}  // namespace higher_order_greeks
}  // namespace quantfinlib
