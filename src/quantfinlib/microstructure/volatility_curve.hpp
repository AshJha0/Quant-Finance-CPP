// Port of com.quantfinlib.microstructure.VolatilityCurve: intraday
// volatility seasonality baseline plus a normalized volatility-regime
// signal. rollDay seeds PER BUCKET (baselineEwma[b] == 0 check) -- a
// bucket first observed on a later day seeds from its own first
// observation rather than ramping from 0 (the deliberate Java bug fix
// this port preserves; contrast VolumeCurve's session-level seed).
// Persistence (persist.Checkpoint) is not ported -- see EwmaCovariance's note.
#pragma once

#include <vector>

namespace quantfinlib {

class VolatilityCurve {
public:
    /**
     * @param buckets  time buckets per session (78 equities, 288 for 24h FX)
     * @param dayAlpha baseline EWMA weight across days, e.g. 0.1
     */
    explicit VolatilityCurve(int buckets = 78, double dayAlpha = 0.1);

    /** Seeds the baseline from a known shape (same units you will feed) -- optional. */
    VolatilityCurve& seedBaseline(const std::vector<double>& volPerBucket);

    /** An observed volatility reading for bucket; non-finite or negative readings are ignored. */
    void onVol(int bucket, double volPerSqrtSecond);

    /** Closes the session: folds today's per-bucket mean vol into the baseline. */
    void rollDay();

    /** The learned time-of-day baseline vol for a bucket (0 until learned). */
    double baseline(int bucket) const { return baselineEwma_[static_cast<std::size_t>(bucket)]; }

    /** Normalized volatility-regime signal: clamp(current/baseline - 1, 0, 1). */
    double regime(int bucket, double currentVolPerSqrtSecond) const;

    int buckets() const { return buckets_; }
    int daysLearned() const { return daysLearned_; }

private:
    int buckets_;
    double dayAlpha_;

    std::vector<double> baselineEwma_;
    std::vector<double> todaySum_;
    std::vector<long> todayCount_;
    int daysLearned_ = 0;
};

}  // namespace quantfinlib
