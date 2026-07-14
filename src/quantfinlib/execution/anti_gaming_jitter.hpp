// Port of com.quantfinlib.execution.AntiGamingJitter: anti-gaming
// randomization for schedule-driven algos -- a TWAP that fires identical
// children on a metronome is a gift to anyone watching the tape. The
// counter-measure is controlled jitter that keeps the SCHEDULE honest
// while killing the pattern:
//
//  - Size jitter: each child +/-sizeFraction, with the differences
//    redistributed so the TOTAL is preserved exactly.
//  - Time jitter: each firing time +/-timeFraction of its own interval,
//    monotonicity preserved and end time never exceeded.
//
// Deterministic per seed (java.util.Random -- bit-exact via JavaRandom) --
// replayable in backtests, auditable in production. Research/warm lane.
// Relationship to TwapScheduler::scheduleRandomized: that jitters TWAP
// slice SIZES at construction; this class is the generic overlay -- it
// jitters any existing child plan and adds the TIME dimension.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/risk/java_random.hpp"

namespace quantfinlib {

class AntiGamingJitter {
public:
    /**
     * @param seed         deterministic seed (replayable, auditable)
     * @param sizeFraction max relative size perturbation, in [0, 0.5]
     * @param timeFraction max relative time perturbation within each
     *                     interval, in [0, 0.5]
     */
    AntiGamingJitter(std::int64_t seed, double sizeFraction, double timeFraction);

    /**
     * Jitters child sizes +/-sizeFraction, preserving the total EXACTLY
     * and never producing a negative child. Perturbations are paired
     * (child i gives what child i+1 takes), so the completion curve
     * wanders inside a one-child envelope of the original.
     */
    std::vector<std::int64_t> jitterSizes(const std::vector<std::int64_t>& childQty);

    /**
     * Jitters firing times within their intervals: each time moves
     * +/-timeFraction of the gap to its neighbors, strict monotonicity
     * preserved, first/last never escape [startNanos, original end].
     */
    std::vector<std::int64_t> jitterTimes(const std::vector<std::int64_t>& timesNanos, std::int64_t startNanos);

private:
    JavaRandom rnd_;
    double sizeFraction_;
    double timeFraction_;
};

}  // namespace quantfinlib
