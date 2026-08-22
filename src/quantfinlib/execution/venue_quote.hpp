// Port of com.quantfinlib.execution.VenueQuote: a venue's dealable top of
// book for routing. For dark venues, bid/ask sizes are the estimated
// executable liquidity and fills are assumed at the venue's midpoint.
#pragma once

#include <cstdint>
#include <limits>
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
    // The feed timestamp (same clock as the `nowNanos` passed to
    // smart_order_router::route) this quote was last refreshed at, so a
    // router can detect a dead/frozen feed. Defaults to "always fresh" for
    // callers that don't track quote age (existing 8-field initializers).
    std::int64_t lastUpdateNanos = std::numeric_limits<std::int64_t>::max();

    double mid() const { return (bid + ask) / 2; }
};

}  // namespace quantfinlib
