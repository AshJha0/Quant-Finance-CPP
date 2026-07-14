// Port of com.quantfinlib.microstructure.VolumeCurve: dynamic intraday
// volume prediction -- a learned per-bucket profile (EWMA across days)
// plus an intraday rescale of the remaining curve by today's
// realized-vs-expected ratio, shrunk toward 1 early in the day. Note the
// deliberate difference from VolatilityCurve/SpreadForecaster: rollDay
// seeds the WHOLE profile from `daysLearned == 0`, not per bucket -- here
// a zero bucket IS a real observation (no prints = no volume), so seeding
// per-bucket would bias sparse-early-day buckets. Persistence
// (persist::Checkpoint): writeState/readState persist the learned profile
// (cross-day state) only; intraday state resets on read. Format version
// 1, matching Java's VolumeCurve.writeState/readState byte for byte.
#pragma once

#include <vector>

#include "quantfinlib/persist/checkpoint.hpp"

namespace quantfinlib {

class VolumeCurve {
public:
    /**
     * @param buckets buckets per session (e.g. 78 five-minute buckets)
     * @param alpha   day-over-day EWMA weight, e.g. 0.1
     */
    explicit VolumeCurve(int buckets = 78, double alpha = 0.1);

    /** Seeds the profile from a known shape (any positive scale) -- optional. */
    VolumeCurve& seedProfile(const std::vector<double>& volumesPerBucket);

    /** Market volume observed in `bucket` (call as prints arrive). */
    void onVolume(int bucket, long qty);

    /** Closes the session: folds today into the learned profile and resets intraday state. */
    void rollDay();

    /** Expected fraction of today's total volume already traded. */
    double expectedFractionElapsed(int bucket, double fracWithinBucket) const;

    /** Projected total volume for today. */
    double projectedDayVolume(int bucket, double fracWithinBucket) const;

    /** Volume still expected between now and the close, under the projection. */
    double expectedVolumeRemaining(int bucket, double fracWithinBucket) const;

    /** Today's realized volume so far. */
    double realizedToday() const { return todayTotal_; }

    /** The learned average volume for one bucket. */
    double profileVolume(int bucket) const { return profileEwma_[static_cast<std::size_t>(bucket)]; }

    int buckets() const { return buckets_; }
    int daysLearned() const { return daysLearned_; }

    /** Persists the learned profile (cross-day state) -- see persist::Checkpoint. */
    void writeState(BinWriter& out) const;

    /**
     * Restores the learned profile; intraday state resets (restore at
     * session start). Throws std::runtime_error if the checkpoint was
     * written with a different bucket count or an unknown state version.
     */
    void readState(BinReader& in);

private:
    void rebuildPrefixSums();

    int buckets_;
    double alpha_;

    std::vector<double> profileEwma_;
    std::vector<double> cumProfile_;
    std::vector<double> today_;
    double todayTotal_ = 0;
    double profileTotal_ = 0;
    int daysLearned_ = 0;
};

}  // namespace quantfinlib
