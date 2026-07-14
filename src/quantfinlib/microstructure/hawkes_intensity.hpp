// Port of com.quantfinlib.microstructure.HawkesIntensity: self-exciting
// (Hawkes) event intensity, lambda(t) = mu + S(t), S updated in O(1) per
// event (decay what is there, add alpha). Stability is enforced in the
// constructor: the branching ratio alpha/beta must be < 1 (beta =
// ln2/halfLife), else the process is explosive and construction throws.
#pragma once

#include <cstdint>

namespace quantfinlib {

class HawkesIntensity {
public:
    /**
     * @param baselineRatePerSec mu, e.g. 2.0
     * @param excitation         alpha, e.g. 0.1
     * @param decayHalfLifeNanos how fast excitation fades, e.g. 2s; throws
     *                           if alpha/beta >= 1 (explosive)
     */
    HawkesIntensity(double baselineRatePerSec = 2.0, double excitation = 0.1,
                     std::int64_t decayHalfLifeNanos = 2'000'000'000LL);

    /** One event arrival; out-of-order (decreasing) timestamps are dropped. */
    void onEvent(std::int64_t timestampNanos);

    /** The current intensity lambda(now) in events per second. */
    double intensity(std::int64_t nowNanos) const;

    /** Dimensionless burst regime: self-excited intensity over baseline, clamped to [0,1]. */
    double burstScore(std::int64_t nowNanos) const;

    std::int64_t events() const { return events_; }

private:
    double excitedAt(std::int64_t nowNanos) const;

    double baselineRatePerSec_;
    double excitation_;
    std::int64_t decayHalfLifeNanos_;

    double excited_ = 0;
    std::int64_t lastEventNanos_ = 0;
    bool hasEvent_ = false;
    std::int64_t events_ = 0;
};

}  // namespace quantfinlib
