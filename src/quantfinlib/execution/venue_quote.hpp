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
    // router can detect a dead/frozen feed. The default means "never
    // stamped": a router with a maximum quote age treats it as stale.
    std::int64_t lastUpdateNanos = std::numeric_limits<std::int64_t>::max();

    double mid() const { return (bid + ask) / 2; }
};

/**
 * Whether a feed stamp is too old to trust at nowNanos. A never-set or future
 * stamp is unknown age, and unknown age counts as stale.
 */
inline bool stampIsStale(std::int64_t stampNanos, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos) {
    if (stampNanos == std::numeric_limits<std::int64_t>::max() || stampNanos > nowNanos) {
        return true;
    }
    // Compared unsigned: nowNanos - stamp can overflow.
    return static_cast<std::uint64_t>(nowNanos) - static_cast<std::uint64_t>(stampNanos)
            > static_cast<std::uint64_t>(maxQuoteAgeNanos);
}

}  // namespace quantfinlib
