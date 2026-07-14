// Port of com.quantfinlib.microstructure.SpreadForecaster: short-term
// spread prediction -- a time-of-day baseline (per-bucket seeded, like
// VolatilityCurve) plus a fast mean-reverting deviation decayed with a
// half-life. Persistence (persist.Checkpoint) is not ported -- see
// EwmaCovariance's note.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class SpreadForecaster {
public:
    /** Per-observation blend weight of the live deviation. */
    static constexpr double DEVIATION_ALPHA = 0.25;

    /**
     * @param buckets                time buckets per session
     * @param dayAlpha               baseline EWMA weight across days, e.g. 0.1
     * @param deviationHalfLifeNanos how fast a spread shock reverts to baseline
     */
    explicit SpreadForecaster(int buckets = 78, double dayAlpha = 0.1,
                               std::int64_t deviationHalfLifeNanos = 5'000'000'000LL);

    /** Seeds the time-of-day baseline from a known shape -- optional. */
    SpreadForecaster& seedBaseline(const std::vector<double>& spreadPerBucket);

    /** Observed spread at bucket; updates the today accumulator and the live deviation. */
    void onSpread(int bucket, double spread, std::int64_t timestampNanos);

    /** Closes the session: folds today's per-bucket mean spreads into the baseline. */
    void rollDay();

    /** Forecast spread at bucket as of nowNanos: baseline plus decayed deviation. */
    double forecast(int bucket, std::int64_t nowNanos) const;

    /** The learned time-of-day baseline spread for a bucket (0 until learned). */
    double baseline(int bucket) const { return baselineEwma_[static_cast<std::size_t>(bucket)]; }

    /** Current deviation from baseline (decayed to now). */
    double currentDeviation(std::int64_t nowNanos) const { return decayedDeviation(nowNanos); }

    int buckets() const { return buckets_; }
    int daysLearned() const { return daysLearned_; }

private:
    double decayedDeviation(std::int64_t now) const;

    int buckets_;
    double dayAlpha_;
    std::int64_t deviationHalfLifeNanos_;

    std::vector<double> baselineEwma_;
    std::vector<double> todaySum_;
    std::vector<long> todayCount_;

    double deviation_ = 0;
    std::int64_t deviationTime_ = 0;
    bool hasDeviation_ = false;
    double lastSpread_;
    int daysLearned_ = 0;
};

}  // namespace quantfinlib
