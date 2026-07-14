// Port of com.quantfinlib.execution.TwapScheduler: TWAP (time-weighted
// average price) schedule design -- splits a parent order into evenly
// spaced child slices, optionally with randomized sizes to reduce
// schedule predictability (anti-gaming). Slice quantities always sum
// exactly to the parent quantity. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/slice.hpp"

namespace quantfinlib {
namespace twap_scheduler {

/** Equal slices at equal intervals starting at t=0. */
std::vector<Slice> schedule(std::int64_t totalQty, std::int64_t durationMillis, int numSlices);

/**
 * Randomized TWAP: slice sizes jittered by up to `jitterPct` (e.g. 0.3 =
 * +/-30%), deterministic for a given seed.
 */
std::vector<Slice> scheduleRandomized(std::int64_t totalQty, std::int64_t durationMillis, int numSlices,
                                      double jitterPct, std::int64_t seed);

/** Shared slicing core (also used by WmrFixingScheduler). */
std::vector<Slice> toSlices(std::int64_t totalQty, std::int64_t durationMillis,
                            const std::vector<double>& weights);

}  // namespace twap_scheduler
}  // namespace quantfinlib
