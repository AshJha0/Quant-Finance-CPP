// Port of com.quantfinlib.execution.PovTracker: POV (percentage-of-volume)
// execution tracker -- the streaming counterpart of the precomputed
// TwapScheduler/VwapScheduler schedules. POV cannot be prescheduled -- it
// chases realized market volume -- so this class maintains the
// participation ledger live: feed it market prints and own fills, and it
// answers "how many shares am I allowed to send right now" (dueQuantity).
//
// The target is executed ~= participation * marketVolume, where
// marketVolume excludes our own fills (participation is measured against
// OTHER people's trading, else the algo chases itself: p/(1+p) instead of
// p). Child sizes are clamped to [minSlice, maxSlice] -- the minimum
// suppresses dribble orders, the maximum caps signaling risk.
//
// Primitives only, zero allocation, single-writer. IllegalArgumentException
// maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <limits>

namespace quantfinlib {

class PovTracker {
public:
    /**
     * @param parentQty     total shares to execute
     * @param participation target participation rate in (0, 1], e.g. 0.1 = 10%
     * @param minSlice      smallest child worth sending (0 = no minimum)
     * @param maxSlice      largest child ever sent (caps information leakage)
     */
    PovTracker(std::int64_t parentQty, double participation, std::int64_t minSlice, std::int64_t maxSlice);

    /** A market trade print that was NOT our fill. */
    void onMarketVolume(std::int64_t qty);

    /** Our own child fill (do not also feed it to onMarketVolume); releases working quantity. */
    void onExecuted(std::int64_t qty);

    /** A child order went out. Throws if it would overfill the parent. */
    void onSent(std::int64_t qty);

    /** Working quantity came back unfilled. */
    void onCancelled(std::int64_t qty);

    /**
     * Shares to send now to restore the target participation: the behind-
     * schedule quantity net of working orders, clamped to the slice bounds
     * and the unsent remainder. Returns 0 while within minSlice of schedule
     * (or when done) -- poll it on every print, act when it's positive. A
     * tail smaller than minSlice goes out whole once the schedule reaches it.
     */
    std::int64_t dueQuantity() const;

    /** Realized participation so far vs other-flow volume (NaN before any print). */
    double realizedParticipation() const;

    std::int64_t executed() const { return executed_; }
    std::int64_t remaining() const { return parentQty_ - executed_; }
    std::int64_t working() const { return working_; }
    std::int64_t marketVolume() const { return marketVolume_; }
    bool done() const { return executed_ >= parentQty_; }

private:
    std::int64_t parentQty_;
    double participation_;
    std::int64_t minSlice_;
    std::int64_t maxSlice_;

    std::int64_t marketVolume_ = 0;
    std::int64_t executed_ = 0;
    std::int64_t working_ = 0;
};

}  // namespace quantfinlib
