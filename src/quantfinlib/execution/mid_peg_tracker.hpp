// Port of com.quantfinlib.execution.MidPegTracker: mid-rate pegging model
// -- tracks the target price of a mid-pegged order with an offset and
// optional limit cap, and decides when the peg has drifted far enough to
// justify a reprice (each reprice costs queue priority and a message, so
// small moves are ignored). IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <limits>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class MidPegTracker {
public:
    /**
     * @param offset           signed offset from the mid (negative = more
     *                         passive for a buy)
     * @param limitPrice       hard cap: never price through this (NaN to disable)
     * @param repriceThreshold minimum absolute peg move before repricing
     */
    MidPegTracker(Side side, double offset, double limitPrice, double repriceThreshold);

    /**
     * Feed a top-of-book update. Returns the new order price when a
     * reprice is warranted, or NaN when the current price should be left
     * alone.
     */
    double onQuote(double bid, double ask);

    /** Current working price (NaN before the first quote). */
    double currentPrice() const { return currentPrice_; }

private:
    Side side_;
    double offset_;
    double limitPrice_;
    double repriceThreshold_;
    double currentPrice_ = std::numeric_limits<double>::quiet_NaN();
};

}  // namespace quantfinlib
