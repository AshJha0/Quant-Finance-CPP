// Port of com.quantfinlib.execution.SmartOrderRouter: splits a marketable
// order across venues to minimize the all-in (fee-adjusted) execution
// price, respecting each venue's displayed/estimated size. Dark venues
// price at their midpoint; with preferDark they are swept first
// regardless of ranking (to minimize information leakage), otherwise
// every venue competes purely on all-in price.
#pragma once

#include <cstdint>
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
 *                         nowNanos - quote.lastUpdateNanos > maxQuoteAgeNanos;
 *                         pass INT64_MAX to disable the staleness check
 */
RoutingPlan route(Side side, std::int64_t quantity, const std::vector<VenueQuote>& venues, bool preferDark,
                  double limitPrice, std::int64_t nowNanos, std::int64_t maxQuoteAgeNanos);

}  // namespace smart_order_router
}  // namespace quantfinlib
