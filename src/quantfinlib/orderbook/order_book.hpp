// Port of com.quantfinlib.orderbook.OrderBook (Java): the research-grade
// price-time-priority limit order book with continuous matching -- the
// readable reference whose behavior IS the specification of HftOrderBook
// (a model-based equivalence test drives both books with identical random
// operation streams and demands identical state). Deliberately favors
// clarity over allocation discipline (std::map levels, per-order objects),
// exactly like the Java original's TreeMap/HashMap.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

/** A resting limit order. Quantity is the remaining (unfilled) amount. */
struct LimitOrder {
    std::int64_t id;
    Side side;
    double price;
    std::int64_t quantity;   // mutated by the book on fills
    std::int64_t timestampNanos;
};

class OrderBook {
public:
    /** Fill callback: maker is the resting order, taker the incoming one. */
    using TradeListener = std::function<void(std::int64_t makerOrderId,
                                             std::int64_t takerOrderId, double price,
                                             std::int64_t quantity,
                                             std::int64_t timestampNanos)>;

    explicit OrderBook(std::string symbol) : symbol_(std::move(symbol)) {}

    const std::string& symbol() const { return symbol_; }

    void addTradeListener(TradeListener listener);

    // ------------------------------------------------------------------
    // Order entry
    // ------------------------------------------------------------------

    /**
     * Submits a limit order: matches any crossing liquidity, then rests the
     * remainder. Returns the order id (usable for cancel() and qtyAhead()
     * while any quantity rests).
     */
    std::int64_t submitLimit(Side side, double price, std::int64_t quantity,
                             std::int64_t timestampNanos);

    /** Submits a market order; any unfilled remainder is discarded. Returns filled quantity. */
    std::int64_t submitMarket(Side side, std::int64_t quantity, std::int64_t timestampNanos);

    /** Cancels a resting order. Returns false if unknown or fully filled. */
    bool cancel(std::int64_t orderId);

    // ------------------------------------------------------------------
    // Top of book / depth
    // ------------------------------------------------------------------

    /** Best bid price; NaN when the bid side is empty. */
    double bestBid() const;
    /** Best ask price; NaN when the ask side is empty. */
    double bestAsk() const;
    std::int64_t bestBidSize() const;
    std::int64_t bestAskSize() const;

    double mid() const;
    double spread() const { return bestAsk() - bestBid(); }

    /**
     * Price/quantity pairs for the given side's best `maxLevels`
     * (side = the resting liquidity side: BUY returns bids).
     */
    std::vector<std::pair<double, std::int64_t>> levels(Side side, int maxLevels) const;

    /** Total resting quantity on a side across its best `maxLevels`. */
    std::int64_t depth(Side side, int maxLevels) const;

    // ------------------------------------------------------------------
    // Queue position & surveillance counters
    // ------------------------------------------------------------------

    /** Quantity queued ahead of the given resting order at its price level; -1 if unknown. */
    std::int64_t qtyAhead(std::int64_t orderId) const;

    /** The resting order, or nullptr when unknown/fully filled/cancelled. */
    const LimitOrder* order(std::int64_t orderId) const;

    std::int64_t orderCount() const { return orderCount_; }
    std::int64_t cancelCount() const { return cancelCount_; }
    std::int64_t tradeCount() const { return tradeCount_; }

    /** Order-to-trade ratio: messages (orders + cancels) per trade. */
    double orderToTradeRatio() const;

private:
    struct PriceLevel {
        double price;
        std::deque<std::int64_t> queue;   // resting order ids, FIFO
        std::int64_t totalQty = 0;
    };

    // Bids sorted best (highest) first; asks best (lowest) first -- the
    // TreeMap comparators of the Java original.
    using Bids = std::map<double, PriceLevel, std::greater<double>>;
    using Asks = std::map<double, PriceLevel>;

    std::int64_t match(Side takerSide, std::int64_t qty, double limitPrice, bool market,
                       std::int64_t takerId, std::int64_t timestampNanos);
    template <typename Book>
    std::int64_t matchAgainst(Book& opposite, Side takerSide, std::int64_t qty,
                              double limitPrice, bool market, std::int64_t takerId,
                              std::int64_t timestampNanos);

    std::string symbol_;
    Bids bids_;
    Asks asks_;
    std::unordered_map<std::int64_t, LimitOrder> orders_;
    std::vector<TradeListener> listeners_;
    std::int64_t nextId_ = 1;
    std::int64_t orderCount_ = 0;
    std::int64_t cancelCount_ = 0;
    std::int64_t tradeCount_ = 0;
};

}  // namespace quantfinlib
