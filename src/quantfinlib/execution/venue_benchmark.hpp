// Port of com.quantfinlib.execution.VenueBenchmark: venue benchmarking
// from execution outcomes -- fill rate, latency-to-fill, effective spread
// paid, and post-trade markout (adverse selection) per venue, ranked by
// execution quality.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class VenueBenchmark {
public:
    /**
     * One routing outcome. For unfilled attempts set filled=false;
     * price/mid/latency fields are then ignored.
     */
    struct Sample {
        std::string venue;
        Side side;
        double price;
        std::int64_t quantity;
        double midAtExec;
        double midAfterHorizon;
        std::int64_t latencyToFillNanos;
        bool filled;
    };

    struct VenueStats {
        std::string venue;
        int attempts;
        double fillRate;
        double avgEffectiveSpreadBps;
        double avgMarkoutBps;
        double avgLatencyToFillMillis;
    };

    VenueBenchmark& add(const Sample& sample);

    /**
     * Per-venue statistics, best execution quality first (lowest effective
     * spread net of markout). Markout is signed so positive = the price
     * moved in our favor after the fill (negative = adverse selection).
     */
    std::vector<VenueStats> rank() const;

private:
    std::vector<Sample> samples_;
};

}  // namespace quantfinlib
