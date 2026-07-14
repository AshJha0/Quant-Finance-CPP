#include "quantfinlib/execution/spread_execution_algo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

SpreadExecutionAlgo::SpreadExecutionAlgo(std::int64_t leadParentQty, double hedgePerLeadUnit,
                                         std::int64_t leggingLimitHedgeUnits, std::int64_t leadChildMax)
        : leadParent_(leadParentQty),
          ratio_(hedgePerLeadUnit),
          leggingLimit_(leggingLimitHedgeUnits),
          leadChildMax_(leadChildMax) {
    if (leadParentQty <= 0) {
        throw std::invalid_argument("leadParentQty must be > 0");
    }
    if (!(hedgePerLeadUnit > 0) || hedgePerLeadUnit == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("ratio must be positive and finite");
    }
    if (leggingLimitHedgeUnits < 1) {
        throw std::invalid_argument("leggingLimit must be >= 1 hedge unit");
    }
    if (static_cast<double>(leggingLimitHedgeUnits) < hedgePerLeadUnit) {
        // floor(limit/ratio) would be 0 at zero imbalance: decide() could
        // never emit a lead child and the algo would livelock.
        throw std::invalid_argument("leggingLimit " + std::to_string(leggingLimitHedgeUnits)
                                     + " cannot cover even one lead unit's hedge (ratio "
                                     + std::to_string(hedgePerLeadUnit) + ") -- execution would be impossible");
    }
    if (leadChildMax <= 0) {
        throw std::invalid_argument("leadChildMax must be > 0");
    }
}

std::int64_t SpreadExecutionAlgo::imbalanceHedgeUnits() const {
    return static_cast<std::int64_t>(std::llround(static_cast<double>(leadExecuted_) * ratio_)) - hedgeExecuted_;
}

SpreadExecutionAlgo::Children SpreadExecutionAlgo::decide() const {
    std::int64_t imbalance = imbalanceHedgeUnits();
    std::int64_t hedgeQty = std::max<std::int64_t>(0, imbalance);
    std::int64_t leadRemaining = leadParent_ - leadExecuted_;
    std::int64_t leadQty;
    bool atCap = imbalance >= leggingLimit_;
    if (atCap) {
        // Stop adding lead risk; the hedge child is the whole imbalance --
        // cross it and get flat.
        leadQty = 0;
    } else {
        // Even a full lead fill with NO hedge fill stays inside the cap.
        auto headroomLeadUnits = static_cast<std::int64_t>(
                std::floor(static_cast<double>(leggingLimit_ - imbalance) / ratio_));
        leadQty = std::min(std::min(leadRemaining, leadChildMax_), headroomLeadUnits);
    }
    return Children{leadQty, hedgeQty, atCap};
}

void SpreadExecutionAlgo::onLeadFill(std::int64_t qty) {
    if (qty < 0 || leadExecuted_ + qty > leadParent_) {
        throw std::invalid_argument("lead fill " + std::to_string(qty) + " overfills parent");
    }
    leadExecuted_ += qty;
}

void SpreadExecutionAlgo::onHedgeFill(std::int64_t qty) {
    auto target = static_cast<std::int64_t>(std::llround(static_cast<double>(leadParent_) * ratio_));
    if (qty < 0 || hedgeExecuted_ + qty > target) {
        throw std::invalid_argument("hedge fill " + std::to_string(qty)
                                     + " would exceed the spread's hedge target " + std::to_string(target));
    }
    hedgeExecuted_ += qty;
}

bool SpreadExecutionAlgo::done() const {
    return leadExecuted_ == leadParent_ && imbalanceHedgeUnits() <= 0;
}

}  // namespace quantfinlib
