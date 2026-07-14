#include "quantfinlib/execution/mid_peg_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

MidPegTracker::MidPegTracker(Side side, double offset, double limitPrice, double repriceThreshold)
        : side_(side), offset_(offset), limitPrice_(limitPrice), repriceThreshold_(repriceThreshold) {
    if (!std::isfinite(offset)) {
        throw std::invalid_argument("offset must be finite, got " + std::to_string(offset));
    }
    // NaN disables the limit by contract; anything else must be finite.
    if (limitPrice == std::numeric_limits<double>::infinity()
            || limitPrice == -std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("limitPrice must be finite or NaN to disable");
    }
    // A negative or NaN threshold makes every quote "beyond threshold":
    // the peg reprices constantly and burns the queue priority it exists
    // to protect.
    if (!(repriceThreshold >= 0) || repriceThreshold == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("repriceThreshold must be >= 0 and finite, got "
                                     + std::to_string(repriceThreshold));
    }
}

double MidPegTracker::onQuote(double bid, double ask) {
    double target = (bid + ask) / 2 + offset_;
    if (!std::isnan(limitPrice_)) {
        target = side_ == Side::BUY ? std::min(target, limitPrice_) : std::max(target, limitPrice_);
    }
    if (std::isnan(currentPrice_) || std::abs(target - currentPrice_) >= repriceThreshold_) {
        currentPrice_ = target;
        return target;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

}  // namespace quantfinlib
