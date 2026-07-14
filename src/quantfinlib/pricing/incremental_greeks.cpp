#include "quantfinlib/pricing/incremental_greeks.hpp"

#include <cmath>
#include <limits>

namespace quantfinlib {

double IncrementalGreeks::nan_() {
    return std::numeric_limits<double>::quiet_NaN();
}

void IncrementalGreeks::reprice(OptionType type, double spot, double strike,
                                double rate, double carry, double vol,
                                double timeYears) {
    black_scholes::Greeks g = black_scholes::greeks(type, spot, strike, rate,
                                                    carry, vol, timeYears);
    baseSpot_ = spot;
    basePrice_ = g.price;
    baseDelta_ = g.delta;
    baseGamma_ = g.gamma;
    baseVega_ = g.vega;
    baseTheta_ = g.theta;
    lastSpot_ = spot;
    estimatedPrice_ = g.price;
    estimatedDelta_ = g.delta;
}

void IncrementalGreeks::onTick(double spot) {
    double dS = spot - baseSpot_;
    estimatedPrice_ = basePrice_ + baseDelta_ * dS + 0.5 * baseGamma_ * dS * dS;
    estimatedDelta_ = baseDelta_ + baseGamma_ * dS;
    lastSpot_ = spot;
}

bool IncrementalGreeks::needsReprice(double maxSpotDrift) const {
    return std::fabs(lastSpot_ - baseSpot_) > maxSpotDrift;
}

double IncrementalGreeks::estimatedPrice() const {
    return estimatedPrice_;
}

double IncrementalGreeks::estimatedDelta() const {
    return estimatedDelta_;
}

double IncrementalGreeks::gamma() const {
    return baseGamma_;
}

double IncrementalGreeks::vega() const {
    return baseVega_;
}

double IncrementalGreeks::theta() const {
    return baseTheta_;
}

double IncrementalGreeks::anchorSpot() const {
    return baseSpot_;
}

}  // namespace quantfinlib
