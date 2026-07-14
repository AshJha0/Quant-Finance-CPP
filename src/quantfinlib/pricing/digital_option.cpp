#include "quantfinlib/pricing/digital_option.hpp"

#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace digital_option {

namespace {

double d2(double spot, double strike, double rate, double carry,
          double vol, double t) {
    return (std::log(spot / strike) + (rate - carry - 0.5 * vol * vol) * t)
            / (vol * std::sqrt(t));
}

void validate(double spot, double strike, double vol, double t) {
    if (spot <= 0 || strike <= 0 || vol <= 0 || t < 0) {
        throw std::invalid_argument(
                "spot, strike, vol must be > 0 and timeYears >= 0");
    }
}

}  // namespace

double cashOrNothing(OptionType type, double spot, double strike,
                     double rate, double carry, double vol,
                     double timeYears, double payout) {
    validate(spot, strike, vol, timeYears);
    if (timeYears == 0) {
        bool itm = type == OptionType::CALL ? spot > strike : spot < strike;
        return itm ? payout : 0;
    }
    double d2v = d2(spot, strike, rate, carry, vol, timeYears);
    double df = std::exp(-rate * timeYears);
    return payout * df * math_utils::normCdf(sign(type) * d2v);
}

double assetOrNothing(OptionType type, double spot, double strike,
                      double rate, double carry, double vol, double timeYears) {
    validate(spot, strike, vol, timeYears);
    if (timeYears == 0) {
        bool itm = type == OptionType::CALL ? spot > strike : spot < strike;
        return itm ? spot : 0;
    }
    double d1 = d2(spot, strike, rate, carry, vol, timeYears)
            + vol * std::sqrt(timeYears);
    return spot * std::exp(-carry * timeYears) * math_utils::normCdf(sign(type) * d1);
}

}  // namespace digital_option
}  // namespace quantfinlib
