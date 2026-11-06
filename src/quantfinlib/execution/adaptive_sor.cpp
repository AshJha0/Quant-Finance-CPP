#include "quantfinlib/execution/adaptive_sor.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/microstructure/queue_model.hpp"

namespace quantfinlib {

AdaptiveSor::Config::Config(double missPenaltyBps_, double urgencyBpsPerMs_, double minFillRate_,
                            std::int64_t defaultDarkProbeShares_, double maxDarkFraction_)
        : missPenaltyBps(missPenaltyBps_),
          urgencyBpsPerMs(urgencyBpsPerMs_),
          minFillRate(minFillRate_),
          defaultDarkProbeShares(defaultDarkProbeShares_),
          maxDarkFraction(maxDarkFraction_) {
    if (missPenaltyBps < 0 || urgencyBpsPerMs < 0 || minFillRate < 0 || minFillRate > 1
            || defaultDarkProbeShares < 0 || maxDarkFraction < 0 || maxDarkFraction > 1) {
        throw std::invalid_argument("invalid router config");
    }
}

AdaptiveSor::Config AdaptiveSor::Config::defaults() {
    return Config(2.0, 1.0, 0.5, 5000, 0.5);
}

AdaptiveSor::AdaptiveSor(VenueScorecard& scorecard, const Config& config)
        : scorecard_(scorecard), config_(config) {}

AdaptiveSor::AdaptiveSor(VenueScorecard& scorecard) : AdaptiveSor(scorecard, Config::defaults()) {}

void AdaptiveSor::registerVenue(const std::string& venue, int scorecardId) {
    if (scorecardId < 0 || scorecardId >= scorecard_.venueCount()) {
        throw std::invalid_argument("scorecardId out of range: " + std::to_string(scorecardId));
    }
    venueIds_[venue] = scorecardId;
}

std::int64_t AdaptiveSor::darkProbeSize(const int* id, std::int64_t quantity) const {
    double learned = id != nullptr ? scorecard_.expectedHiddenShares(*id) : 0;
    std::int64_t base = learned > 0 ? static_cast<std::int64_t>(std::llround(learned))
                                    : config_.defaultDarkProbeShares;
    return std::min(base, static_cast<std::int64_t>(std::llround(config_.maxDarkFraction
                                                                * static_cast<double>(quantity))));
}

namespace {
struct Scored {
    std::string venue;
    double price;
    double expected;
    std::int64_t size;
};
}  // namespace

AdaptiveSor::RoutingDecision AdaptiveSor::route(Side side, std::int64_t quantity,
                                                const std::vector<VenueQuote>& venues,
                                                const smart_order_router::RouteGuard& guard) {
    smart_order_router::requireGuard(guard);
    return routeChecked(side, quantity, venues, guard.limitPrice, guard.nowNanos, guard.maxQuoteAgeNanos);
}

AdaptiveSor::RoutingDecision AdaptiveSor::route(Side side, std::int64_t quantity,
                                                const std::vector<VenueQuote>& venues) {
    return routeChecked(side, quantity, venues, smart_order_router::marketLimit(side), 0,
                        std::numeric_limits<std::int64_t>::max());
}

AdaptiveSor::RoutingDecision AdaptiveSor::route(Side side, std::int64_t quantity,
                                                const std::vector<VenueQuote>& venues,
                                                double limitPrice, std::int64_t nowNanos,
                                                std::int64_t maxQuoteAgeNanos) {
    return routeChecked(side, quantity, venues, limitPrice, nowNanos, maxQuoteAgeNanos);
}

AdaptiveSor::RoutingDecision AdaptiveSor::routeChecked(Side side, std::int64_t quantity,
                                                       const std::vector<VenueQuote>& venues,
                                                       double limitPrice, std::int64_t nowNanos,
                                                       std::int64_t maxQuoteAgeNanos) {
    smart_order_router::requireRouteArguments(limitPrice, maxQuoteAgeNanos);
    bool buy = side == Side::BUY;
    std::vector<smart_order_router::RouteLeg> lit;
    std::vector<smart_order_router::RouteLeg> probes;
    std::vector<Scored> candidates;
    candidates.reserve(venues.size());

    for (const VenueQuote& v : venues) {
        if (smart_order_router::isStale(v, nowNanos, maxQuoteAgeNanos)) {
            continue;
        }
        auto idIt = venueIds_.find(v.venue);
        const int* id = idIt != venueIds_.end() ? &idIt->second : nullptr;
        if (v.dark) {
            std::int64_t probe = darkProbeSize(id, quantity);
            if (probe > 0 && v.bid > 0 && v.ask > 0
                && smart_order_router::isDealable(side, v.mid(), limitPrice)) {
                probes.push_back(smart_order_router::RouteLeg{v.venue, v.mid(), probe, true});
            }
            continue;
        }
        double px = buy ? v.ask : v.bid;
        std::int64_t size = buy ? v.askSize : v.bidSize;
        if (size <= 0 || !smart_order_router::isDealable(side, px, limitPrice) || !std::isfinite(v.feeBps)) {
            continue;
        }
        double fillRate = id != nullptr ? scorecard_.fillRate(*id) : scorecard_.fillRatePrior();
        if (id != nullptr && fillRate < config_.minFillRate) {
            continue;  // reliability veto
        }
        double latencyNanos = static_cast<double>(v.latencyNanos);
        if (id != nullptr && scorecard_.measuredLatencyNanos(*id) > 0) {
            latencyNanos = scorecard_.measuredLatencyNanos(*id);
        }
        double allIn = px * (1 + (buy ? 1 : -1) * v.feeBps / 1e4);
        double missAdj = (1 - fillRate) * config_.missPenaltyBps / 1e4;
        double latAdj = latencyNanos / 1e6 * config_.urgencyBpsPerMs / 1e4;
        // A venue whose fills revert charges that reversion per share;
        // favorable/unknown markout adds nothing (no bonus for luck).
        double adverseAdj = 0;
        if (id != nullptr) {
            double markout = scorecard_.postFillMarkout(*id);
            if (markout < 0) {
                adverseAdj = -markout / px;
            }
        }
        double expected = allIn * (1 + (buy ? 1 : -1) * (missAdj + latAdj + adverseAdj));
        candidates.push_back(Scored{v.venue, px, expected, size});
    }

    // Displayed price first, so no scorecard preference trades through a
    // better quote; the expected cost ranks venues showing the same price.
    std::stable_sort(candidates.begin(), candidates.end(), [buy](const Scored& a, const Scored& b) {
        if (a.price != b.price) {
            return buy ? a.price < b.price : a.price > b.price;
        }
        return buy ? a.expected < b.expected : a.expected > b.expected;
    });

    std::int64_t remaining = quantity;
    for (const Scored& c : candidates) {
        if (remaining <= 0) {
            break;
        }
        std::int64_t take = std::min(remaining, c.size);
        lit.push_back(smart_order_router::RouteLeg{c.venue, c.price, take, false});
        remaining -= take;
    }
    return RoutingDecision{lit, probes, quantity - remaining, remaining};
}

double AdaptiveSor::passiveFillProbability(std::int64_t qtyAhead, std::int64_t orderQty,
                                           double expectedTradedQty) {
    return queue_model::fillProbability(qtyAhead, orderQty, expectedTradedQty);
}

}  // namespace quantfinlib
