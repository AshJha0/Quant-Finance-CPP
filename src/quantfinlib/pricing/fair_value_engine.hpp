// Port of com.quantfinlib.pricing.FairValueEngine (Java): latency-adjusted
// fair value for rapidly updating order books. Maintains the size-weighted
// microprice plus a short-window mid drift estimate, so a consumer that is
// latencyNanos behind the market can project the "true mid" at the moment
// its order would actually arrive. Zero allocation after construction
// (fixed ring of samples); single writer.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class FairValueEngine {
public:
    /**
     * @param capacity    ring capacity (rounded up to a power of two)
     * @param windowNanos lookback window for the drift estimate
     */
    FairValueEngine(int capacity, std::int64_t windowNanos);

    /** 256-sample ring over a 500 ms drift window. */
    FairValueEngine();

    /** Feed a top-of-book update. */
    void onQuote(double bid, double ask, double bidSize, double askSize,
                 std::int64_t timestampNanos);

    /** Size-weighted microprice: I*ask + (1-I)*bid, I = bidSize/(bidSize+askSize). */
    static double microprice(double bid, double ask, double bidSize, double askSize);

    /** Latest microprice (NaN before the first quote). */
    double microprice() const;

    /** Estimated mid drift in price units per second over the lookback window. */
    double driftPerSecond() const;

    /**
     * Fair price projected latencyNanos into the future: microprice plus
     * drift over the latency horizon.
     */
    double latencyAdjustedFair(std::int64_t latencyNanos) const;

private:
    std::vector<double> mids_;
    std::vector<std::int64_t> times_;
    int mask_;
    std::int64_t windowNanos_;
    std::int64_t head_ = 0;  // number of samples ever written
    double microprice_;
};

}  // namespace quantfinlib
