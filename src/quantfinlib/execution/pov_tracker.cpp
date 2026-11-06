#include "quantfinlib/execution/pov_tracker.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

PovTracker::PovTracker(std::int64_t parentQty, double participation, std::int64_t minSlice,
                       std::int64_t maxSlice)
        : parentQty_(parentQty), participation_(participation), minSlice_(minSlice), maxSlice_(maxSlice) {
    if (parentQty <= 0 || participation <= 0 || participation > 1) {
        throw std::invalid_argument("need parentQty > 0, participation in (0,1]");
    }
    if (minSlice < 0 || maxSlice < std::max<std::int64_t>(1, minSlice)) {
        throw std::invalid_argument("need 0 <= minSlice <= maxSlice");
    }
}

void PovTracker::onMarketVolume(std::int64_t qty) {
    if (qty > 0) {
        marketVolume_ += qty;
    }
}

void PovTracker::onExecuted(std::int64_t qty) {
    if (qty > 0) {
        executed_ += qty;
        working_ -= std::min(working_, qty);
    }
}

void PovTracker::onSent(std::int64_t qty) {
    if (qty <= 0) {
        return;
    }
    if (qty > parentQty_ - executed_ - working_) {
        throw std::logic_error("child order would overfill the parent");
    }
    working_ += qty;
}

void PovTracker::onCancelled(std::int64_t qty) {
    if (qty > 0) {
        working_ -= std::min(working_, qty);
    }
}

std::int64_t PovTracker::dueQuantity() const {
    std::int64_t committed = executed_ + working_;
    std::int64_t remaining = parentQty_ - committed;
    if (remaining <= 0) {
        return 0;
    }
    auto target = static_cast<std::int64_t>(participation_ * static_cast<double>(marketVolume_));
    std::int64_t behind = target - committed;
    if (behind < std::max<std::int64_t>(1, std::min(minSlice_, remaining))) {
        return 0;
    }
    return std::min(std::min(behind, maxSlice_), remaining);
}

double PovTracker::realizedParticipation() const {
    return marketVolume_ == 0 ? std::nan("") : static_cast<double>(executed_) / static_cast<double>(marketVolume_);
}

}  // namespace quantfinlib
