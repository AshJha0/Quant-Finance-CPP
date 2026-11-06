// Port of com.quantfinlib.execution.VwapScheduler: VWAP schedule design --
// allocates child slices proportionally to an expected intraday volume
// profile (e.g. from a learned VolumeCurve), so participation tracks the
// market's own volume curve. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/slice.hpp"

namespace quantfinlib {
namespace vwap_scheduler {

/**
 * @param totalQty       parent order quantity
 * @param volumeProfile  expected volume per bucket (any positive scale);
 *                       an all-zero profile throws: it says nothing about
 *                       when to trade, so there is nothing to follow
 * @param durationMillis total execution window; slice i starts at
 *                       i * duration / buckets
 */
std::vector<Slice> schedule(std::int64_t totalQty, const std::vector<double>& volumeProfile,
                            std::int64_t durationMillis);

/**
 * Largest-remainder proportional allocation: integer quantities that sum
 * exactly to `total`, proportional to `weights`. Weights summing to zero throw.
 */
std::vector<std::int64_t> allocateProportionally(std::int64_t total, const std::vector<double>& weights);

/**
 * Rounds a schedule to whole lots without losing shares: each slice takes
 * the lots its cumulative quantity has earned, the part below one lot
 * carries into the next slice, and the last slice takes the odd remainder.
 * The total is unchanged.
 */
std::vector<Slice> roundToLots(const std::vector<Slice>& slices, std::int64_t lotSize);

}  // namespace vwap_scheduler
}  // namespace quantfinlib
