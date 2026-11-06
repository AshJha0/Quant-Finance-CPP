// Port of com.quantfinlib.execution.SmartOrderRouter: splits a marketable
// order across venues to minimize the all-in (fee-adjusted) execution
// price, respecting each venue's displayed/estimated size. Dark venues
// price at their midpoint; with preferDark they are swept first
// regardless of ranking (to minimize information leakage), otherwise
// every venue competes purely on all-in price.
//
// DEVIATION FROM JAVA: a non-positive price is never routable; venue latency
// costs latencyBpsPerMs in the ranking; lit legs execute in displayed-price
// order, so a low fee cannot route through a better displayed quote (Reg NMS
// Rule 611 in spirit); fees then rank venues showing the same price.
#pragma once

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "quantfinlib/execution/venue_quote.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {
namespace smart_order_router {

/** One child order of the routing plan. `price` is the quoted (pre-fee) price. */
struct RouteLeg {
    std::string venue;
    double price;
    std::int64_t quantity;
    bool dark;
};

struct RoutingPlan {
    std::vector<RouteLeg> legs;
    std::int64_t routedQty;
    std::int64_t unroutedQty;
    double expectedAvgAllInPrice;
};

/** Default ranking charge for latency; the same figure as AdaptiveSor's default. */
constexpr double DEFAULT_LATENCY_BPS_PER_MS = 1.0;

/** Whether nothing newer than maxQuoteAgeNanos backs this quote. An unset or future stamp counts as stale. */
bool isStale(const VenueQuote& quote, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos);

/** True when the price is a real price (finite, > 0) inside limitPrice. */
bool isDealable(Side side, double price, double limitPrice);

/** Throws unless limitPrice is a number and maxQuoteAgeNanos >= 0. */
void requireRouteArguments(double limitPrice, std::int64_t maxQuoteAgeNanos);

/** The limit that means "pure market" for this side. */
double marketLimit(Side side);

/** Everything the router can be told beyond the order itself. */
struct RouteOptions {
    double limitPrice;                 // use marketLimit(side) for none
    std::int64_t nowNanos = 0;
    std::int64_t maxQuoteAgeNanos = std::numeric_limits<std::int64_t>::max();  // max disables
    double latencyBpsPerMs = DEFAULT_LATENCY_BPS_PER_MS;
    bool allowTradeThrough = false;    // true restores pure all-in ordering of lit venues
};

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark);

/**
 * As above, but skips any venue whose price would violate `limitPrice`
 * (through-the-limit on the relevant side) -- the double-price analogue of
 * HftSor::route's limitTick.
 *
 * @param limitPrice worst acceptable raw (pre-fee) price; pass
 *                    +infinity (buy) / -infinity (sell) for pure market
 */
RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice);

/**
 * As above, additionally excluding any venue whose quote is stale -- the
 * same treatment a NaN or zero-size venue already gets, mirroring
 * HftSor::venueDown as the way a dead/frozen feed is kept from receiving
 * child orders.
 *
 * @param nowNanos         current time, same clock as VenueQuote::lastUpdateNanos
 * @param maxQuoteAgeNanos a venue is excluded when
 *                         nowNanos - quote.lastUpdateNanos > maxQuoteAgeNanos,
 *                         or its stamp is unset or in the future;
 *                         pass INT64_MAX to disable the staleness check
 */
RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos);

RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  const RouteOptions& options);

}  // namespace smart_order_router
}  // namespace quantfinlib
