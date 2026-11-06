#include "quantfinlib/execution/dark_pool_simulator.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

void DarkPoolSimulator::onQuote(double bid, double ask) {
    mid_ = (bid > 0 && ask > 0 && bid < ask && ask != std::numeric_limits<double>::infinity())
            ? (bid + ask) / 2
            : std::numeric_limits<double>::quiet_NaN();
}

std::vector<DarkPoolSimulator::Fill> DarkPoolSimulator::submit(Side side, std::int64_t quantity,
                                                               std::int64_t minExecutionQty) {
    if (quantity <= 0) {
        throw std::invalid_argument("quantity must be positive, got " + std::to_string(quantity));
    }
    if (minExecutionQty < 0) {
        throw std::invalid_argument("minExecutionQty must be >= 0, got " + std::to_string(minExecutionQty));
    }
    std::int64_t id = nextId_++;
    std::vector<Fill> fills;
    std::int64_t remaining = quantity;
    if (!std::isnan(mid_)) {
        std::list<Resting>& contra = side == Side::BUY ? sells_ : buys_;
        // Dry-run the exact time-priority consumption below to learn the
        // aggregate quantity that would actually execute. A static scan
        // against the ORIGINAL remaining overcounts: a later resting
        // order's MEQ can become unsatisfiable once earlier fills shrink
        // the remainder, and crossing on the inflated total would violate
        // the INCOMING order's aggregate MEQ.
        std::int64_t crossable = 0;
        std::int64_t dryRemaining = remaining;
        for (const Resting& r : contra) {
            if (dryRemaining == 0) {
                break;
            }
            std::int64_t fill = std::min(dryRemaining, r.qty);
            if (fill < r.minQty) {
                continue;
            }
            crossable += fill;
            dryRemaining -= fill;
        }
        if (crossable >= minExecutionQty) {
            auto it = contra.begin();
            while (it != contra.end() && remaining > 0) {
                std::int64_t fill = std::min(remaining, it->qty);
                if (fill < it->minQty) {
                    ++it;  // the RESTING order's own constraint
                    continue;
                }
                it->qty -= fill;
                it->minQty = 0;
                remaining -= fill;
                fills.push_back(side == Side::BUY ? Fill{id, it->id, mid_, fill}
                                                  : Fill{it->id, id, mid_, fill});
                if (it->qty == 0) {
                    it = contra.erase(it);
                } else {
                    ++it;
                }
            }
        }
    }
    if (remaining > 0) {
        (side == Side::BUY ? buys_ : sells_).push_back(
                Resting{id, fills.empty() ? minExecutionQty : 0, remaining});
    }
    return fills;
}

std::int64_t DarkPoolSimulator::restingQty(Side side) const {
    std::int64_t total = 0;
    for (const Resting& r : (side == Side::BUY ? buys_ : sells_)) {
        total += r.qty;
    }
    return total;
}

}  // namespace quantfinlib
