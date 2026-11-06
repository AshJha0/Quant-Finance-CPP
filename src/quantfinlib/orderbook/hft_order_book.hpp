// Port of com.quantfinlib.orderbook.HftOrderBook (Java): venue-grade
// price-time-priority matching engine, the zero-allocation sibling of
// OrderBook. Dense integer-tick price ladder, occupancy bitmaps, pooled
// intrusive order nodes in parallel primitive arrays, a primitive
// open-addressing id map with backward-shift deletion, and a single
// function-pointer TradeSink (the Java primitive callback interface).
//
// No allocation after construction: every submit/cancel/query touches
// only the arrays sized in the constructor (proven by the counting
// operator-new test, the C++ analogue of Java's ThreadMXBean assert).
//
// Threading: single-threaded by design, like real matching cores -- one
// engine thread owns the book; fan orders in via trading::OrderRingBuffer
// and trades out via the sink. Rejections are return codes, never
// exceptions, because a venue must not unwind its matching loop over a
// bad order.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class HftOrderBook {
public:
    /** Accepted-order ids are positive; these are the rejection codes. */
    static constexpr std::int64_t REJECT_POOL_FULL = -1;
    static constexpr std::int64_t REJECT_OUT_OF_BAND = -2;
    static constexpr std::int64_t REJECT_INVALID = -3;
    /** Post-only order would have crossed the spread and taken liquidity. */
    static constexpr std::int64_t REJECT_WOULD_CROSS = -4;
    /** Quantity above maxOrderQuantity(). */
    static constexpr std::int64_t REJECT_QUANTITY_LIMIT = -5;

    /** Largest pool: the id map must stay addressable by a positive int. */
    static constexpr int MAX_ORDERS = 1 << 29;
    /** Largest ladder, in price levels per side. */
    static constexpr std::int64_t MAX_LADDER = 1LL << 30;

    /** Best-price sentinels when a side is empty (Java Integer.MIN/MAX_VALUE). */
    static constexpr int NO_BID = INT32_MIN;
    static constexpr int NO_ASK = INT32_MAX;

    /**
     * Primitive fill callback: maker is the resting order, taker the
     * incoming one. A raw function pointer plus context so installing and
     * invoking the sink never allocates (Java's @FunctionalInterface).
     */
    using TradeSink = void (*)(void* context, std::int64_t makerOrderId,
                               std::int64_t takerOrderId, int priceTick,
                               std::int64_t quantity, std::int64_t timestampNanos);

    /**
     * @param minPriceTick lowest representable price, in ticks (inclusive)
     * @param maxPriceTick highest representable price, in ticks (inclusive)
     * @param maxOrders    resting-order capacity (pool size, fixed forever)
     * @throws std::invalid_argument unless maxPriceTick >= minPriceTick, the
     *         ladder fits MAX_LADDER, neither bound is a NO_BID/NO_ASK
     *         sentinel value and 0 < maxOrders <= MAX_ORDERS
     */
    HftOrderBook(int minPriceTick, int maxPriceTick, int maxOrders);

    /**
     * Installs the (single) trade callback; call before trading. The sink
     * runs in the middle of matching, so it must not call back into this book
     * for anything but openQuantity().
     */
    void tradeSink(TradeSink sink, void* context);

    // ------------------------------------------------------------------
    // Order entry (the hot path)
    // ------------------------------------------------------------------

    /**
     * Limit order: matches against the opposite side while it crosses, rests
     * any remainder at `priceTick`. Returns the positive order id on
     * acceptance (also the taker id in emitted trades -- even a fully filled
     * order was accepted), or a negative rejection code. Zero allocation.
     * REJECT_POOL_FULL can follow real fills: the trades stand and only the
     * remainder is dropped.
     */
    std::int64_t submitLimit(Side side, int priceTick, std::int64_t quantity,
                             std::int64_t timestampNanos);

    /**
     * Market order: matches against the whole opposite book, never rests.
     * Returns the filled quantity (0 when the opposite side is empty, or
     * when quantity is non-positive or above maxOrderQuantity()).
     */
    std::int64_t submitMarket(Side side, std::int64_t quantity, std::int64_t timestampNanos);

    /**
     * Immediate-or-cancel: a price-limited taker -- matches while it crosses,
     * and the remainder expires instead of resting. Returns the filled
     * quantity (0 when nothing crossed, or quantity is non-positive or above
     * maxOrderQuantity()). Zero allocation.
     */
    std::int64_t submitIoc(Side side, int priceTick, std::int64_t quantity,
                           std::int64_t timestampNanos);

    /**
     * Fill-or-kill: executes the full quantity within the limit price or
     * does nothing at all. Returns `quantity` on fill, 0 on kill -- a killed
     * order emits no trades and consumes no id/counters, like a venue
     * rejecting pre-match. Zero allocation (the liquidity probe walks the
     * same occupancy bitmaps as matching).
     */
    std::int64_t submitFok(Side side, int priceTick, std::int64_t quantity,
                           std::int64_t timestampNanos);

    /**
     * Post-only (add-liquidity-only) limit order: rests at `priceTick`, or
     * is rejected with REJECT_WOULD_CROSS when it would trade on arrival --
     * the maker-fee-preserving order type. Returns the positive order id on
     * acceptance.
     */
    std::int64_t submitPostOnly(Side side, int priceTick, std::int64_t quantity,
                                std::int64_t timestampNanos);

    /** Cancels a resting order. False when unknown/already gone -- never throws. */
    bool cancel(std::int64_t orderId);

    // ------------------------------------------------------------------
    // Queries (all primitive, zero allocation)
    // ------------------------------------------------------------------

    /** Best bid in absolute ticks; NO_BID when no bids. */
    int bestBidTick() const;

    /** Best ask in absolute ticks; NO_ASK when no asks. */
    int bestAskTick() const;

    std::int64_t bestBidSize() const;
    std::int64_t bestAskSize() const;

    /** Resting quantity at an absolute tick (0 when off-band or empty). */
    std::int64_t qtyAtTick(Side side, int priceTick) const;

    /** Open (unfilled, uncancelled) quantity of an order; 0 when gone. */
    std::int64_t openQuantity(std::int64_t orderId) const;

    /**
     * Depth snapshot into caller-provided arrays (absolute ticks + level
     * quantities, best-first): zero allocation. Returns levels written
     * (bounded by maxLevels).
     */
    int snapshot(Side side, int* outTicks, std::int64_t* outQtys, int maxLevels) const;

    /**
     * Largest quantity one order may carry: INT64_MAX / maxOrders. Every
     * resting order is at most this, and at most maxOrders rest, so neither a
     * level total nor the FOK liquidity sum can overflow.
     */
    std::int64_t maxOrderQuantity() const { return maxOrderQuantity_; }

    std::int64_t orderCount() const { return orderCount_; }
    std::int64_t cancelCount() const { return cancelCount_; }
    std::int64_t tradeCount() const { return tradeCount_; }

    /** Orders currently resting in the book (pool slots in use). */
    int restingOrders() const { return restingOrders_; }

private:
    static constexpr std::uint8_t kBuy = 0;
    static constexpr std::uint8_t kSell = 1;
    static constexpr int kNone = -1;

    std::int64_t matchBuy(std::int64_t takerId, int limitIdx, std::int64_t quantity,
                          std::int64_t ts);
    std::int64_t matchSell(std::int64_t takerId, int limitIdx, std::int64_t quantity,
                           std::int64_t ts);
    std::int64_t fillLevel(int* head, int* tail, std::int64_t* levelQty, std::uint64_t* bits,
                           int idx, std::int64_t takerId, std::int64_t remaining,
                           std::int64_t ts);
    void rest(std::int64_t id, std::uint8_t side, int idx, std::int64_t quantity);
    void unlink(int node);
    void freeNode(int node);
    bool validQuantity(std::int64_t quantity) const {
        return quantity > 0 && quantity <= maxOrderQuantity_;
    }
    /** Ladder slot of an absolute tick, or -1 off-band; never wraps. */
    int ladderIdx(int priceTick) const;
    bool fillableWithin(Side side, int priceTick, std::int64_t quantity) const;
    int buyLimitIdx(int priceTick) const;
    int sellLimitIdx(int priceTick) const;

    const int minTick_;
    const int ladder_;                       // number of price levels per side
    const std::int64_t maxOrderQuantity_;

    // Per-side level queues: head/tail node index per tick slot, total resting qty.
    std::vector<int> bidHead_;
    std::vector<int> bidTail_;
    std::vector<std::int64_t> bidQty_;
    std::vector<int> askHead_;
    std::vector<int> askTail_;
    std::vector<std::int64_t> askQty_;
    // Occupancy bitmaps: bit i set = level i has resting orders on that side.
    std::vector<std::uint64_t> bidBits_;
    std::vector<std::uint64_t> askBits_;
    int bestBidIdx_ = kNone;                 // highest occupied bid slot
    int bestAskIdx_ = kNone;                 // lowest occupied ask slot

    // Intrusive node pool (parallel primitive arrays; index = node handle).
    std::vector<std::int64_t> nodeId_;
    std::vector<std::int64_t> nodeQty_;
    std::vector<int> nodeTick_;              // ladder slot, not absolute tick
    std::vector<int> nodePrev_;
    std::vector<int> nodeNext_;
    std::vector<std::uint8_t> nodeSide_;
    int freeHead_;                           // free-list threaded through nodeNext_

    // Open-addressing id -> node map (linear probing, backward-shift delete).
    // Key 0 = empty slot, which is safe because ids start at 1.
    std::vector<std::int64_t> mapKeys_;
    std::vector<int> mapVals_;
    int mapMask_;

    TradeSink sink_ = nullptr;
    void* sinkContext_ = nullptr;
    std::int64_t nextId_ = 1;
    std::int64_t orderCount_ = 0;
    std::int64_t cancelCount_ = 0;
    std::int64_t tradeCount_ = 0;
    int restingOrders_ = 0;
};

}  // namespace quantfinlib
