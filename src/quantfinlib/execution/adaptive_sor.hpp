// Port of com.quantfinlib.execution.AdaptiveSor: the full-checklist smart
// order router -- where SmartOrderRouter ranks venues purely on
// fee-adjusted displayed price and HftSor does the same at tick-path
// speed, this router prices in everything a production SOR actually
// weighs: displayed liquidity and fees/rebates, probability of fill
// (VenueScorecard fill rate discounts the quote, and venues below a
// reliability floor are vetoed outright), latency (scorecard measured
// latency overrides the advertised VenueQuote::latencyNanos once
// observed), adverse selection (a venue whose fills revert charges that
// reversion per share), hidden liquidity (dark pools probed with sizes
// learned from realized probe fills), and queue position (via
// passiveFillProbability, delegating to QueueModel).
//
// Probe legs are additive and contingent: the executor sends them
// alongside the lit legs and, on dark fills, cancels lit remainder -- the
// router plans, the executor manages overfill.
//
// Research/decision lane (allocates the plan); venue identity is by name,
// mapped once to dense scorecard ids at registration.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/execution/smart_order_router.hpp"
#include "quantfinlib/execution/venue_quote.hpp"
#include "quantfinlib/execution/venue_scorecard.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class AdaptiveSor {
public:
    /** Tunable penalties; defaults() is a sane starting point. */
    struct Config {
        double missPenaltyBps;
        double urgencyBpsPerMs;
        double minFillRate;
        std::int64_t defaultDarkProbeShares;
        double maxDarkFraction;

        Config(double missPenaltyBps_, double urgencyBpsPerMs_, double minFillRate_,
              std::int64_t defaultDarkProbeShares_, double maxDarkFraction_);

        /**
         * missPenalty 2 bps (~ re-cross half a spread), urgency 1 bp/ms of
         * latency, veto below 50% fill rate, 5,000-share default dark
         * probe, dark capped at half the parent.
         */
        static Config defaults();
    };

    /**
     * The routed plan: `lit` legs cover up to the requested quantity
     * (best-expected-cost first); `probes` are additive contingent dark
     * legs sent alongside; `unrouted` is the shortfall no eligible lit
     * venue could absorb (0 on a fully routed order).
     */
    struct RoutingDecision {
        std::vector<smart_order_router::RouteLeg> lit;
        std::vector<smart_order_router::RouteLeg> probes;
        std::int64_t routedQty;
        std::int64_t unrouted;
    };

    AdaptiveSor(VenueScorecard& scorecard, const Config& config);
    explicit AdaptiveSor(VenueScorecard& scorecard);

    /**
     * Maps a venue name to its scorecard index. Register every venue once
     * at setup; quotes from unregistered venues are routed on quote data
     * alone (prior fill rate, advertised latency).
     */
    void registerVenue(const std::string& venue, int scorecardId);

    /** The scorecard this router learns from (feed fills/misses/probes to it). */
    VenueScorecard& scorecard() { return scorecard_; }

    /**
     * Routes a marketable parent of `quantity`. Lit venues are ranked by
     * expected cost per share and swept best-first at displayed size.
     * Every quoting dark venue gets a contingent probe leg at its
     * midpoint, sized by learned hidden liquidity (or the configured
     * default while unknown), capped at maxDarkFraction * quantity.
     */
    RoutingDecision route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues);

    /**
     * As above, with the limit and staleness rules of
     * smart_order_router::route: a venue through limitPrice, or stale at
     * nowNanos, gets neither lit legs nor dark probes.
     */
    RoutingDecision route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues,
                          double limitPrice, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos);

    /**
     * Fill probability for a PASSIVE child joining a queue with
     * `qtyAhead` ahead of it -- the queue-position leg of the routing
     * checklist, delegated to QueueModel.
     */
    static double passiveFillProbability(std::int64_t qtyAhead, std::int64_t orderQty,
                                         double expectedTradedQty);

private:
    std::int64_t darkProbeSize(const int* id, std::int64_t quantity) const;

    VenueScorecard& scorecard_;
    Config config_;
    std::unordered_map<std::string, int> venueIds_;
};

}  // namespace quantfinlib
