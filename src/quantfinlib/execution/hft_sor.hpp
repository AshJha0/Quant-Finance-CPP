// Port of com.quantfinlib.execution.HftSor: hot-lane smart order router --
// the zero-allocation sibling of SmartOrderRouter, for when the routing
// decision sits on the tick-to-order path. Venue state lives in parallel
// primitive arrays (updated in place from your per-venue feeds); a route
// decision is a greedy sweep by all-in price with no sorting, no lists.
//
// Fees are expressed in ticks (venue taker fee converted at the
// instrument's tick size, once, at configuration time), so the comparison
// stays in integer-friendly arithmetic. Negative fee ticks model rebates.
// With the handful of venues real equities routing faces, the O(V^2)
// selection sweep is faster than any heap -- and allocates nothing.
//
// Single-writer; reuse one router per routing thread.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class HftSor {
public:
    explicit HftSor(int venueCount);

    /** Per-venue taker fee in ticks (negative = rebate). Configure once. */
    void fee(int venue, double takerFeeTicks);

    /** One venue's displayed top of book (zero size = side unavailable). */
    void venueQuote(int venue, std::int32_t bid, std::int64_t bidSz, std::int32_t ask, std::int64_t askSz);

    /**
     * Removes a venue from routing (feed loss / venue halt) -- the
     * symmetric call to Nbbo.onVenueDown, so a dead venue's stale quote
     * can never keep receiving child orders.
     */
    void venueDown(int venue);

    /**
     * Routes a marketable order across venues by best all-in price (quote
     * +/- fee), splitting at displayed size. Child quantities are written
     * into outQty[venue]: the array must be at least venueCount() long and
     * indices [0, venueCount) are fully overwritten (entries beyond that
     * are untouched). The return value is the total routed quantity --
     * anything short of quantity found no displayed liquidity. Zero
     * allocation.
     *
     * @param limitTick worst acceptable raw price in ticks (before fees);
     *                  pass INT32_MAX (buy) / INT32_MIN (sell) for pure
     *                  market
     */
    std::int64_t route(Side side, std::int64_t quantity, std::int32_t limitTick, std::int64_t* outQty);

    int venueCount() const { return venues_; }
    std::int64_t routeCount() const { return routeCount_; }

private:
    int venues_;
    std::vector<std::int32_t> bidTick_;
    std::vector<std::int64_t> bidSize_;
    std::vector<std::int32_t> askTick_;
    std::vector<std::int64_t> askSize_;
    std::vector<double> feeTicks_;

    std::int64_t routeCount_ = 0;
};

}  // namespace quantfinlib
