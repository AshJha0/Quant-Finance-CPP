// Port of com.quantfinlib.execution.IcebergOrder: iceberg order state
// machine — shows only a small display tranche of the full quantity and
// reloads automatically when the visible portion fills. Display sizes
// can be randomized (seeded SplittableRandom, bit-compatible with Java)
// to make the iceberg harder to detect. Gates map to
// std::invalid_argument.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

#include "quantfinlib/backtest/validation/splittable_random.hpp"

namespace quantfinlib {

class IcebergOrder {
public:
    IcebergOrder(std::int64_t totalQty, std::int64_t displayQty)
            : IcebergOrder(totalQty, displayQty, 0, 0) {}

    /** @param randomizePct display-size jitter, e.g. 0.2 = ±20% (0 = fixed) */
    IcebergOrder(std::int64_t totalQty, std::int64_t displayQty, double randomizePct,
                 std::int64_t seed)
            : displayQty_(displayQty),
              randomizePct_(randomizePct),
              rnd_(seed),
              remaining_(totalQty) {
        if (totalQty <= 0 || displayQty <= 0) {
            throw std::invalid_argument("quantities must be positive");
        }
        visible_ = nextTranche();
    }

    /**
     * Records a fill against the visible tranche. Returns true when the
     * tranche was exhausted and a fresh one was loaded (i.e. the working
     * order should be re-submitted at the back of the queue).
     */
    bool onFill(std::int64_t qty) {
        if (qty <= 0 || qty > visible_) {
            throw std::invalid_argument("fill " + std::to_string(qty) + " exceeds visible "
                                        + std::to_string(visible_));
        }
        visible_ -= qty;
        remaining_ -= qty;
        if (visible_ == 0 && remaining_ > 0) {
            visible_ = nextTranche();
            return true;
        }
        return false;
    }

    std::int64_t visibleQty() const { return visible_; }
    std::int64_t hiddenQty() const { return remaining_ - visible_; }
    std::int64_t remainingQty() const { return remaining_; }

    bool isComplete() const { return remaining_ == 0; }

private:
    std::int64_t nextTranche() {
        std::int64_t base = displayQty_;
        if (randomizePct_ > 0) {
            // Java Math.round (floor(x + 0.5)) on a positive argument.
            double jittered = static_cast<double>(displayQty_)
                              * (1 + randomizePct_ * (2 * rnd_.nextDouble() - 1));
            base = std::max<std::int64_t>(
                    1, static_cast<std::int64_t>(std::floor(jittered + 0.5)));
        }
        return std::min(base, remaining_);
    }

    std::int64_t displayQty_;
    double randomizePct_;
    SplittableRandom rnd_;
    std::int64_t remaining_;   // total unexecuted (visible + hidden)
    std::int64_t visible_ = 0; // currently displayed tranche
};

}  // namespace quantfinlib
