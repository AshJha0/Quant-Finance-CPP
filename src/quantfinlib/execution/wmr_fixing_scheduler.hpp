// Port of com.quantfinlib.execution.WmrFixingScheduler: benchmark-fixing
// execution schedule, WMR-style -- orders benchmarked to a fixing (the
// WM/Refinitiv 4pm London fix and its cousins) are executed by spreading
// the parent evenly across the fixing's calculation window, so realized
// cost tracks the benchmark rather than betting against it -- the window
// is 5 minutes for major pairs, and the benchmark is computed from
// observations inside it, so TWAP-in-window IS the neutral replication.
//
// Deliberately excluded, with reasons: executing ahead of the window to
// "get done early" is pre-hedging risk against the client's benchmark (the
// conduct the 2013-15 FX fix scandals were about), and skewing inside the
// window is a bet, not benchmark replication. Anyone who wants a bet can
// use ImplementationShortfallScheduler and own it explicitly.
//
// Research-lane (allocates the schedule list); the slices it emits are
// executed by the hot lane. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/slice.hpp"

namespace quantfinlib {
namespace wmr_fixing_scheduler {

/** The standard WMR calculation window for major pairs. */
constexpr std::int64_t WINDOW_MILLIS = 5 * 60 * 1000LL;

/**
 * Even slices across the fixing window. Offsets are relative to the
 * schedule's own start; align slice 0 with the window open (fix time
 * minus half the window).
 *
 * @param totalQty     parent quantity (sliced exactly, largest-remainder)
 * @param windowMillis calculation-window length (WINDOW_MILLIS for majors)
 * @param numSlices    child count; more slices = closer benchmark
 *                     tracking, more tickets
 */
std::vector<Slice> schedule(std::int64_t totalQty, std::int64_t windowMillis, int numSlices);

/** schedule() with the standard 5-minute window. */
std::vector<Slice> schedule(std::int64_t totalQty, int numSlices);

}  // namespace wmr_fixing_scheduler
}  // namespace quantfinlib
