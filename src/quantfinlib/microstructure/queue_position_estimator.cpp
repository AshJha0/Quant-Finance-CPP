#include "quantfinlib/microstructure/queue_position_estimator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/microstructure/queue_model.hpp"

namespace quantfinlib {

void QueuePositionEstimator::join(std::int64_t levelSize, std::int64_t ownQty) {
    if (levelSize < 0 || ownQty <= 0) {
        throw std::invalid_argument("need levelSize >= 0, ownQty > 0");
    }
    ahead_ = static_cast<double>(levelSize);
    aheadAtJoin_ = static_cast<double>(levelSize);
    levelSize_ = static_cast<double>(levelSize + ownQty);
    ownQty_ = ownQty;
    active_ = true;
}

void QueuePositionEstimator::onTrade(std::int64_t tradedQty) {
    if (!active_ || tradedQty <= 0) {
        return;
    }
    ahead_ = std::max(0.0, ahead_ - static_cast<double>(tradedQty));
    levelSize_ = std::max(static_cast<double>(ownQty_), levelSize_ - static_cast<double>(tradedQty));
}

void QueuePositionEstimator::onLevelResize(std::int64_t newLevelSize) {
    if (!active_) {
        return;
    }
    double removed = levelSize_ - ownQty_ - std::max(0.0, static_cast<double>(newLevelSize) - ownQty_);
    if (removed > 0) {
        double others = levelSize_ - ownQty_;
        double fracAhead = others > 0 ? ahead_ / others : 0;
        ahead_ = std::max(0.0, ahead_ - removed * fracAhead);
    }
    levelSize_ = std::max(static_cast<double>(ownQty_), static_cast<double>(newLevelSize));
}

void QueuePositionEstimator::onOwnFill(std::int64_t filledQty) {
    if (!active_ || filledQty <= 0) {
        return;
    }
    std::int64_t taken = std::min(filledQty, ownQty_);
    ownQty_ -= taken;
    levelSize_ = std::max(static_cast<double>(ownQty_), levelSize_ - static_cast<double>(taken));
    active_ = ownQty_ > 0;
}

double QueuePositionEstimator::fillProbability(double expectedTradedQty) const {
    return queue_model::fillProbability(std::llround(ahead_), ownQty_, expectedTradedQty);
}

double QueuePositionEstimator::queueProgress() const {
    return aheadAtJoin_ <= 0 ? 1.0 : std::max(0.0, 1 - ahead_ / aheadAtJoin_);
}

}  // namespace quantfinlib
