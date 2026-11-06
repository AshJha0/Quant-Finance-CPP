#include "quantfinlib/execution/smart_order_router.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {
namespace smart_order_router {

namespace {

struct Candidate {
    const VenueQuote* quote;
    double price;
    double allInPrice;
    double rank;  // all-in price with the latency charge, for ordering only
    std::int64_t size;
};

}  // namespace

double marketLimit(Side side) {
    return side == Side::BUY ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
}

bool isStale(const VenueQuote& quote, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos) {
    constexpr std::int64_t DISABLED = std::numeric_limits<std::int64_t>::max();
    if (maxQuoteAgeNanos == DISABLED) {
        return false;
    }
    return stampIsStale(quote.lastUpdateNanos, nowNanos, maxQuoteAgeNanos);
}

bool isDealable(Side side, double price, double limitPrice) {
    if (!(price > 0) || !std::isfinite(price)) {
        return false;
    }
    return side == Side::BUY ? price <= limitPrice : price >= limitPrice;
}

void requireRouteArguments(double limitPrice, std::int64_t maxQuoteAgeNanos) {
    if (std::isnan(limitPrice)) {
        throw std::invalid_argument("limitPrice must not be NaN");
    }
    if (maxQuoteAgeNanos < 0) {
        throw std::invalid_argument("maxQuoteAgeNanos must be >= 0");
    }
}

void requireGuard(const RouteGuard& guard) {
    if (!(guard.limitPrice > 0) || !std::isfinite(guard.limitPrice)) {
        throw std::invalid_argument("a live route needs a finite limit price");
    }
    if (guard.maxQuoteAgeNanos < 0 || guard.maxQuoteAgeNanos == std::numeric_limits<std::int64_t>::max()) {
        throw std::invalid_argument("a live route needs a real maximum quote age");
    }
}

namespace {

RoutingPlan routeChecked(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues,
                         bool preferDark, const RouteOptions& options);

}  // namespace

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  const RouteGuard& guard) {
    requireGuard(guard);
    RouteOptions options;
    options.limitPrice = guard.limitPrice;
    options.nowNanos = guard.nowNanos;
    options.maxQuoteAgeNanos = guard.maxQuoteAgeNanos;
    return routeChecked(side, quantity, venues, preferDark, options);
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark) {
    RouteOptions options;
    options.limitPrice = marketLimit(side);
    return routeChecked(side, quantity, venues, preferDark, options);
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice) {
    RouteOptions options;
    options.limitPrice = limitPrice;
    return routeChecked(side, quantity, venues, preferDark, options);
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos) {
    RouteOptions options;
    options.limitPrice = limitPrice;
    options.nowNanos = nowNanos;
    options.maxQuoteAgeNanos = maxQuoteAgeNanos;
    return routeChecked(side, quantity, venues, preferDark, options);
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  const RouteOptions& options) {
    return routeChecked(side, quantity, venues, preferDark, options);
}

namespace {

RoutingPlan routeChecked(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues,
                         bool preferDark, const RouteOptions& options) {
    if (quantity < 0) {
        throw std::invalid_argument("quantity must be >= 0");
    }
    requireRouteArguments(options.limitPrice, options.maxQuoteAgeNanos);
    if (!(options.latencyBpsPerMs >= 0)) {
        throw std::invalid_argument("latencyBpsPerMs must be >= 0");
    }
    int sgn = sign(side);
    std::vector<Candidate> candidates;
    candidates.reserve(venues.size());
    for (const VenueQuote& v : venues) {
        // A dark mid is only as good as both sides of the reference.
        bool twoSided = v.bid > 0 && v.ask > 0;
        double price = v.dark ? (twoSided ? v.mid() : std::nan("")) : (side == Side::BUY ? v.ask : v.bid);
        std::int64_t size = side == Side::BUY ? v.askSize : v.bidSize;
        if (size <= 0 || !isDealable(side, price, options.limitPrice) || !std::isfinite(v.feeBps)) {
            continue;
        }
        if (isStale(v, options.nowNanos, options.maxQuoteAgeNanos)) {
            continue;  // stale quote, excluded from routing
        }
        double allIn = price * (1 + sgn * v.feeBps / 1e4);
        double latencyBps = static_cast<double>(v.latencyNanos) / 1e6 * options.latencyBpsPerMs;
        double rank = allIn * (1 + sgn * latencyBps / 1e4);
        candidates.push_back(Candidate{&v, price, allIn, rank, size});
    }
    // Best first: lowest for a buy, highest for a sell.
    auto byPrice = [sgn](const Candidate& a, const Candidate& b) {
        return sgn * a.rank < sgn * b.rank;
    };
    if (preferDark) {
        std::stable_sort(candidates.begin(), candidates.end(), [&](const Candidate& a, const Candidate& b) {
            bool ka = !a.quote->dark;
            bool kb = !b.quote->dark;
            if (ka != kb) {
                return ka < kb;
            }
            return byPrice(a, b);
        });
    } else {
        std::stable_sort(candidates.begin(), candidates.end(), byPrice);
    }
    if (!options.allowTradeThrough) {
        // Keep the slots the ranking gave to lit venues, but fill them in
        // displayed-price order. Dark fills at the mid cannot trade through.
        std::vector<std::size_t> slots;
        std::vector<Candidate> lit;
        for (std::size_t i = 0; i < candidates.size(); i++) {
            if (!candidates[i].quote->dark) {
                slots.push_back(i);
                lit.push_back(candidates[i]);
            }
        }
        std::stable_sort(lit.begin(), lit.end(), [sgn](const Candidate& a, const Candidate& b) {
            return sgn * a.price < sgn * b.price;
        });
        for (std::size_t i = 0; i < slots.size(); i++) {
            candidates[slots[i]] = lit[i];
        }
    }

    std::vector<RouteLeg> legs;
    std::int64_t remaining = quantity;
    double allInNotional = 0;
    for (const Candidate& c : candidates) {
        if (remaining <= 0) {
            break;
        }
        std::int64_t take = std::min(remaining, c.size);
        legs.push_back(RouteLeg{c.quote->venue, c.price, take, c.quote->dark});
        allInNotional += static_cast<double>(take) * c.allInPrice;
        remaining -= take;
    }
    std::int64_t routed = quantity - remaining;
    return RoutingPlan{legs, routed, remaining, routed == 0 ? std::nan("") : allInNotional / routed};
}

}  // namespace

}  // namespace smart_order_router
}  // namespace quantfinlib
