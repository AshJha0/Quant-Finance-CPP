#include "quantfinlib/pricing/forward_curve.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {

ForwardCurve::ForwardCurve(double spot) : spot_(spot) {
    if (!(spot > 0)) {
        throw std::invalid_argument("spot must be positive");
    }
    outrights_[0.0] = spot;
}

ForwardCurve& ForwardCurve::addPoint(double tenorYears, double outrightForward) {
    if (!(tenorYears > 0)) {
        throw std::invalid_argument("tenor must be positive");
    }
    outrights_[tenorYears] = outrightForward;
    return *this;
}

double ForwardCurve::spot() const {
    return spot_;
}

double ForwardCurve::forward(double tenorYears) const {
    if (!(tenorYears > 0)) {
        return spot_;
    }
    auto hi = outrights_.lower_bound(tenorYears);  // ceilingEntry
    if (hi == outrights_.end()) {
        // Extrapolate using the slope of the last two pillars (the spot
        // pillar at t = 0 guarantees a predecessor exists).
        auto last = std::prev(outrights_.end());
        auto prev = std::prev(last);
        double slope = (last->second - prev->second) / (last->first - prev->first);
        return last->second + slope * (tenorYears - last->first);
    }
    if (hi->first == tenorYears) {
        return hi->second;
    }
    auto lo = std::prev(hi);                        // floorEntry (t > 0 => exists)
    double w = (tenorYears - lo->first) / (hi->first - lo->first);
    return lo->second + w * (hi->second - lo->second);
}

double ForwardCurve::forwardPoints(double tenorYears) const {
    return forward(tenorYears) - spot_;
}

double ForwardCurve::impliedRateDifferential(double tenorYears) const {
    return std::log(forward(tenorYears) / spot_) / tenorYears;
}

double ForwardCurve::theoreticalForward(double spot, double domesticRate,
                                        double foreignRate, double tenorYears) {
    return spot * (1 + domesticRate * tenorYears) / (1 + foreignRate * tenorYears);
}

double ForwardCurve::mispricingBps(double tenorYears, double domesticRate,
                                   double foreignRate) const {
    double theoretical = theoreticalForward(spot_, domesticRate, foreignRate,
                                            tenorYears);
    return (forward(tenorYears) - theoretical) / theoretical * 1e4;
}

}  // namespace quantfinlib
