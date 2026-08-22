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
    std::int64_t size;
};

double sideDefaultLimit(Side side) {
    return side == Side::BUY ? std::numeric_limits<double>::infinity() : -std::numeric_limits<double>::infinity();
}

}  // namespace

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark) {
    return route(side, quantity, venues, preferDark, sideDefaultLimit(side));
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice) {
    return route(side, quantity, venues, preferDark, limitPrice, 0, std::numeric_limits<std::int64_t>::max());
}

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos) {
    if (quantity < 0) {
        throw std::invalid_argument("quantity must be >= 0");
    }
    int sgn = sign(side);
    std::vector<Candidate> candidates;
    candidates.reserve(venues.size());
    for (const VenueQuote& v : venues) {
        double price = v.dark ? v.mid() : (side == Side::BUY ? v.ask : v.bid);
        std::int64_t size = side == Side::BUY ? v.askSize : v.bidSize;
        if (size <= 0 || std::isnan(price) || !std::isfinite(v.feeBps)) {
            continue;
        }
        if (nowNanos - v.lastUpdateNanos > maxQuoteAgeNanos) {
            continue;  // stale quote, excluded from routing
        }
        if (sgn > 0 ? price > limitPrice : price < limitPrice) {
            continue;  // through the limit
        }
        double allIn = price * (1 + sgn * v.feeBps / 1e4);
        candidates.push_back(Candidate{&v, price, allIn, size});
    }
    // Best all-in price first: lowest for a buy, highest for a sell.
    auto byPrice = [sgn](const Candidate& a, const Candidate& b) {
        return sgn * a.allInPrice < sgn * b.allInPrice;
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

}  // namespace smart_order_router
}  // namespace quantfinlib
