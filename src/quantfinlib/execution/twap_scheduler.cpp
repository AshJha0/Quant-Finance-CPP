#include "quantfinlib/execution/twap_scheduler.hpp"

#include <limits>
#include <stdexcept>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/execution/vwap_scheduler.hpp"

namespace quantfinlib {
namespace twap_scheduler {

std::vector<Slice> schedule(std::int64_t totalQty, std::int64_t durationMillis, int numSlices) {
    std::vector<double> weights(static_cast<std::size_t>(numSlices), 1.0);
    return toSlices(totalQty, durationMillis, weights);
}

std::vector<Slice> scheduleRandomized(std::int64_t totalQty, std::int64_t durationMillis, int numSlices,
                                      double jitterPct, std::int64_t seed) {
    SplittableRandom rnd(seed);
    std::vector<double> weights(static_cast<std::size_t>(numSlices), 0.0);
    for (int i = 0; i < numSlices; i++) {
        weights[static_cast<std::size_t>(i)] = 1 + jitterPct * (2 * rnd.nextDouble() - 1);
    }
    return toSlices(totalQty, durationMillis, weights);
}

std::vector<Slice> toSlices(std::int64_t totalQty, std::int64_t durationMillis,
                            const std::vector<double>& weights) {
    if (weights.empty() || totalQty <= 0) {
        throw std::invalid_argument("need positive quantity and at least one slice");
    }
    auto n = static_cast<std::int64_t>(weights.size());
    // Same overflow guard WmrFixingScheduler carries: a pathological
    // horizon must not wrap durationMillis * i negative and place slices
    // before the window opens.
    if (durationMillis < 0 || durationMillis > std::numeric_limits<std::int64_t>::max() / n) {
        throw std::invalid_argument("durationMillis out of range for " + std::to_string(n) + " slices");
    }
    std::vector<std::int64_t> quantities = vwap_scheduler::allocateProportionally(totalQty, weights);
    std::vector<Slice> out;
    out.reserve(weights.size());
    for (std::int64_t i = 0; i < n; i++) {
        out.push_back(Slice{durationMillis * i / n, quantities[static_cast<std::size_t>(i)]});
    }
    return out;
}

}  // namespace twap_scheduler
}  // namespace quantfinlib
