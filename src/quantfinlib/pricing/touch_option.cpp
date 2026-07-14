#include "quantfinlib/pricing/touch_option.hpp"

#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace touch_option {

namespace {

void validate(double spot, double barrier, double vol, double t) {
    if (spot <= 0 || barrier <= 0 || vol <= 0 || t < 0) {
        throw std::invalid_argument(
                "spot, barrier, vol must be > 0 and timeYears >= 0");
    }
}

}  // namespace

double hitProbability(double spot, double barrier, double rate,
                      double carry, double vol, double timeYears) {
    validate(spot, barrier, vol, timeYears);
    if (spot == barrier) {
        return 1;  // already touching
    }
    if (timeYears == 0) {
        return 0;
    }
    double m = rate - carry - 0.5 * vol * vol;
    double h = std::log(barrier / spot);
    double sq = vol * std::sqrt(timeYears);
    double drift = m * timeYears;
    if (h > 0) {
        // Upper barrier: reflect around +h.
        return math_utils::normCdf((-h + drift) / sq)
                + std::exp(2 * m * h / (vol * vol))
                        * math_utils::normCdf((-h - drift) / sq);
    }
    // Lower barrier: mirror image.
    return math_utils::normCdf((h - drift) / sq)
            + std::exp(2 * m * h / (vol * vol))
                    * math_utils::normCdf((h + drift) / sq);
}

double oneTouch(double spot, double barrier, double rate, double carry,
                double vol, double timeYears, double payout) {
    return payout * std::exp(-rate * timeYears)
            * hitProbability(spot, barrier, rate, carry, vol, timeYears);
}

double noTouch(double spot, double barrier, double rate, double carry,
               double vol, double timeYears, double payout) {
    return payout * std::exp(-rate * timeYears)
            * (1 - hitProbability(spot, barrier, rate, carry, vol, timeYears));
}

}  // namespace touch_option
}  // namespace quantfinlib
