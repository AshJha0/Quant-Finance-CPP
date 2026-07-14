// Port of com.quantfinlib.execution.VenueScorecard: streaming per-venue
// execution quality -- the equities counterpart of fx.LpScorecard:
// displayed prices tell you where a venue CLAIMS you'll trade; the
// scorecard tells you what actually happens when you send there. Tracks:
//
//  - Fill rate: EWMA of {1 fill, 0 miss} per marketable child.
//  - Response latency: EWMA of send->ack/fill time as YOU measure it.
//  - Hidden liquidity: for dark venues, an EWMA of shares each probe found.
//  - Post-fill markout (adverse selection): what the mid does one horizon
//    after your fill, signed in your trading direction; positive = clean
//    fill, negative = you traded against informed/stale flow. Armed via
//    the extended onFill(venue, responseNanos, buy, midAtFill, timestamp)
//    and matured via onMid. The markout leg makes the card single-symbol:
//    onMid matures every pending fill against the one mid it is given, in
//    absolute price units -- one card per symbol once you use it.
//
// All statistics are exponentially weighted per event so the card tracks
// current behavior (each EWMA seeds from the prior / first observation, so
// a venue is never scored below "never tried" for its first fill). Zero
// allocation per event, single writer (the execution-events thread).
//
// This is the streaming venue-quality tool for the routing hot path;
// VenueBenchmark is the batch counterpart for post-trade analysis.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/persist/checkpoint.hpp"

namespace quantfinlib {

class VenueScorecard {
public:
    /** Pending fill-markout slots per venue (bursts deeper overwrite oldest). */
    static constexpr int PENDING_RING = 4;

    /**
     * @param venueCount          number of venues (dense indices)
     * @param alpha               EWMA weight per event, e.g. 0.05
     * @param fillRatePrior       fill rate assumed before any data, e.g. 0.95
     * @param markoutHorizonNanos how long after a fill the markout is
     *                            read, e.g. 100ms = 100'000'000L
     */
    VenueScorecard(int venueCount, double alpha, double fillRatePrior, std::int64_t markoutHorizonNanos);

    /** 100 ms markout horizon. */
    VenueScorecard(int venueCount, double alpha, double fillRatePrior);

    /** 5% event weight, 0.95 prior, 100 ms markout horizon. */
    explicit VenueScorecard(int venueCount);

    // ------------------------------------------------------------ Event feed

    /** A marketable child filled (fully or partially counts as a fill). */
    void onFill(int venue, std::int64_t responseNanos);

    /** A marketable child that came back unfilled (faded, rejected, expired). */
    void onMiss(int venue, std::int64_t responseNanos);

    /**
     * A fill WITH the context that arms its markout: direction, the mid at
     * fill time, and the fill timestamp. Feed mids via onMid and the move
     * one horizon later is attributed to this venue. A NaN mid still
     * counts the fill but can never start a markout.
     */
    void onFill(int venue, std::int64_t responseNanos, bool buy, double midAtFill, std::int64_t timestampNanos);

    /**
     * Mid update FOR THE CARD'S ONE SYMBOL: matures every pending fill
     * markout whose horizon has elapsed.
     */
    void onMid(double mid, std::int64_t timestampNanos);

    /**
     * A dark probe's outcome: how many shares it actually found (0 is a
     * real observation). The estimate seeds from the first probe rather
     * than ramping from 0.
     */
    void onDarkProbe(int venue, std::int64_t sharesFilled);

    // ------------------------------------------------------------ The card

    /** EWMA fill probability; the optimistic prior before any data. */
    double fillRate(int venue) const;

    /** The before-any-data prior (also what unregistered venues score as). */
    double fillRatePrior() const { return fillRatePrior_; }

    /** EWMA measured response latency in nanos (0 before any data). */
    double measuredLatencyNanos(int venue) const;

    /** EWMA shares found per dark probe (0 before any probe). */
    double expectedHiddenShares(int venue) const;

    /**
     * EWMA post-fill markout in price units -- positive means the mid
     * kept moving your way after fills at this venue; negative means it
     * reverted. 0 before any matured markout.
     */
    double postFillMarkout(int venue) const;

    /**
     * Fill markouts matured across all venues -- the wiring canary: zero
     * while fills accrue means onMid is not being fed.
     */
    std::int64_t maturedFillMarkouts() const { return maturedFillMarkouts_; }

    std::int64_t sent(int venue) const;
    std::int64_t filled(int venue) const;
    std::int64_t probes(int venue) const;
    int venueCount() const { return venueCount_; }

    // ------------------------------------------------------------ Persistence

    /**
     * Persists the learned venue quality -- fill rates, measured
     * latencies, dark-probe estimates and fill markouts are exactly what a
     * router should not have to relearn every morning. Format version 2
     * (version 1, from before the markout existed, is still readable).
     */
    void writeState(BinWriter& out) const;

    /**
     * Restores the card; pending fill markouts (intraday) reset. Reads
     * both format versions: a v1 checkpoint restores everything it has and
     * leaves the markout state cold. Throws on a venue-count mismatch or
     * an unknown version.
     */
    void readState(BinReader& in);

private:
    void seedOnFirstEvent(int venue, std::int64_t responseNanos);

    int venueCount_;
    double alpha_;
    double fillRatePrior_;
    std::int64_t markoutHorizonNanos_;

    std::vector<std::int64_t> sent_;
    std::vector<std::int64_t> filled_;
    std::vector<double> fillRateEwma_;
    std::vector<double> latencyNanosEwma_;
    std::vector<double> hiddenFillEwma_;
    std::vector<std::int64_t> probes_;

    std::vector<double> markoutEwma_;
    std::vector<std::int64_t> markoutCount_;
    std::vector<std::int8_t> pendingSide_;  // 0 empty, +1 buy, -1 sell
    std::vector<double> pendingMid_;
    std::vector<std::int64_t> pendingTs_;
    std::vector<std::uint8_t> pendingCursor_;
    int pendingCount_ = 0;
    std::int64_t maturedFillMarkouts_ = 0;
};

}  // namespace quantfinlib
