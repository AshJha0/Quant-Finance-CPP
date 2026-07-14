// Port of com.quantfinlib.execution.VenueQuote: a venue's dealable top of
// book for routing. For dark venues, bid/ask sizes are the estimated
// executable liquidity and fills are assumed at the venue's midpoint.
#pragma once

#include <cstdint>
#include <string>

namespace quantfinlib {

struct VenueQuote {
    std::string venue;
    double bid;
    std::int64_t bidSize;
    double ask;
    std::int64_t askSize;
    double feeBps;
    std::int64_t latencyNanos;
    bool dark;

    double mid() const { return (bid + ask) / 2; }
};

}  // namespace quantfinlib
