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

}  // namespace smart_order_router
}  // namespace quantfinlib
