// Port of com.quantfinlib.marketdata.L3BookBuilder (Java): participant-side
// full-depth (L3) book builder -- reconstructs a venue's book from an
// ITCH-style event stream (add / execute / cancel / delete / replace / trade)
// and answers the questions an execution engine actually asks: best
// bid/ask, depth, and exactly how many shares are queued ahead of my
// order -- with zero allocation on every event.
//
// Same engineering as orderbook::HftOrderBook (dense tick ladder,
// occupancy bitmaps, pooled intrusive nodes, open-addressing ref map with
// backward-shift deletion), but driven by the feed instead of by
// matching: this is the consumer of a venue's L3 feed, not the venue.
//
// Queue position: call track() with your own order's reference (learned
// from the order-entry gateway's ack) once its add has appeared on the
// feed. The initial shares-ahead is computed by one walk of the level's
// FIFO; from then on it is maintained in O(1) per event using two facts
// of price-time priority: executions always consume the queue head (so
// any execution at your level that isn't you happened ahead of you), and
// a cancel is ahead of you iff it entered the queue before you (insertion
// sequence numbers). When your order fills, is deleted, or is replaced,
// tracking ends automatically.
//
// Threading: single-writer -- one feed-handler thread owns the book.
// Order references must be positive (0 is the map's empty sentinel),
// which matches real ITCH feeds.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/marketdata/itch_codec.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class L3BookBuilder {
public:
    /**
     * @param stockLocate  the feed's locate code for the symbol this book tracks;
     *                     messages for other locates are ignored by onMessage
     * @param minPriceTick lowest representable price in 0.0001 ticks (inclusive)
     * @param maxPriceTick highest representable price in 0.0001 ticks (inclusive)
     * @param maxOrders    resting-order capacity (pool size, fixed forever)
     * @throws std::invalid_argument unless maxPriceTick >= minPriceTick and maxOrders > 0
     */
    L3BookBuilder(int stockLocate, int minPriceTick, int maxPriceTick, int maxOrders);

    // ------------------------------------------------------------------
    // Feed entry points (the hot path)
    // ------------------------------------------------------------------

    /**
     * Applies one wire message starting at `offset`. Returns the wire
     * length consumed, or 0 when the message is for another stock locate
     * or outside the supported subset (callers skip it by its own length).
     */
    int onMessage(const unsigned char* buf, int offset);

    /**
     * Add order: appends to its level's FIFO. False when the order was
     * dropped (off-band price, exhausted pool, or a duplicate ref -- a
     * feed anomaly that would otherwise corrupt the ref map).
     */
    bool onAdd(std::int64_t ref, Side side, std::int64_t shares, int priceTick);

    /**
     * Execution against a resting order (always the queue head under
     * price-time priority -- which is what makes O(1) queue tracking sound).
     */
    void onExecute(std::int64_t ref, std::int64_t shares);

    /** Partial cancel: reduces a resting order in place (keeps its priority). */
    void onCancel(std::int64_t ref, std::int64_t shares);

    /** Full removal of a resting order. */
    void onDelete(std::int64_t ref);

    /**
     * Cancel/replace: the original order is removed and the new reference
     * joins the back of the (possibly different) level's queue -- priority
     * is lost, exactly as on a real venue. A replace re-pricing to an
     * off-band level drops the order entirely, consistent with off-band
     * adds: prices outside the configured band are invisible to this book
     * by design.
     */
    void onReplace(std::int64_t origRef, std::int64_t newRef, std::int64_t shares, int priceTick);

    /** Off-book/non-displayed trade print: records it, book unchanged. */
    void onTrade(int priceTick);

    // ------------------------------------------------------------------
    // Own-order queue tracking
    // ------------------------------------------------------------------

    /**
     * Starts queue tracking for a resting order (yours, learned from your
     * gateway ack). The initial shares-ahead is one FIFO walk; maintenance
     * is O(1) per event afterwards. Returns false when the ref is unknown
     * or the tracking table (64 orders) is full.
     */
    bool track(std::int64_t ref);

    /** Stops tracking a ref (no-op when not tracked). */
    void untrack(std::int64_t ref);

    /**
     * Shares queued ahead of a tracked order right now; -1 when the ref is
     * not tracked (never was, or it filled / was deleted / was replaced).
     */
    std::int64_t sharesAhead(std::int64_t ref) const;

    // ------------------------------------------------------------------
    // Queries
    // ------------------------------------------------------------------

    /** Best bid in absolute 0.0001 ticks; INT32_MIN when none. */
    int bestBidTick() const;

    /** Best ask in absolute 0.0001 ticks; INT32_MAX when none. */
    int bestAskTick() const;

    std::int64_t bestBidSize() const;
    std::int64_t bestAskSize() const;

    /** Resting quantity at an absolute tick (0 when off-band or empty). */
    std::int64_t qtyAtTick(Side side, int priceTick) const;

    /** Open shares of any resting order by ref; 0 when gone/unknown. */
    std::int64_t openQuantity(std::int64_t ref) const;

    /** Depth snapshot into caller arrays, best-first; returns levels written. */
    int snapshot(Side side, int* outTicks, std::int64_t* outQtys, int maxLevels) const;

    int lastTradeTick() const { return lastTradeTick_; }
    int restingOrders() const { return restingOrders_; }
    std::int64_t addCount() const { return addCount_; }
    std::int64_t executeCount() const { return executeCount_; }
    std::int64_t cancelCount() const { return cancelCount_; }
    std::int64_t deleteCount() const { return deleteCount_; }
    std::int64_t replaceCount() const { return replaceCount_; }
    std::int64_t tradeCount() const { return tradeCount_; }

    /** Events referencing unknown orders (feed gap symptom -- resubscribe/snapshot). */
    std::int64_t unknownRefCount() const { return unknownRefCount_; }

    /**
     * Orders dropped for off-band prices or an exhausted pool -- adds AND
     * replace re-adds (widen the band/pool; off-band liquidity is
     * invisible to this book by design, and its later events count as
     * unknown refs).
     */
    std::int64_t outOfBandCount() const { return outOfBandCount_; }

    /** Adds re-delivering a live ref, rejected to protect the book (replay symptom). */
    std::int64_t duplicateRefCount() const { return duplicateRefCount_; }

private:
    static constexpr std::uint8_t kBuy = 0;
    static constexpr std::uint8_t kSell = 1;
    static constexpr int kNone = -1;
    static constexpr int kMaxTracked = 64;

    bool insert(std::int64_t ref, Side side, std::int64_t shares, int priceTick);
    bool reduce(std::int64_t ref, std::int64_t shares, bool bySeq);
    void removeWhole(int node, int slot);
    void removeNode(int node, int mapSlot);
    void creditAhead(int node, std::int64_t qty, bool bySeq);
    std::vector<std::int64_t>& levelQtyOf(int node);

    const int stockLocate_;
    const int minTick_;
    const int ladder_;

    std::vector<int> bidHead_;
    std::vector<int> bidTail_;
    std::vector<std::int64_t> bidQty_;
    std::vector<int> askHead_;
    std::vector<int> askTail_;
    std::vector<std::int64_t> askQty_;
    std::vector<std::uint64_t> bidBits_;
    std::vector<std::uint64_t> askBits_;
    int bestBidIdx_ = kNone;
    int bestAskIdx_ = kNone;

    // Pooled intrusive nodes (index = handle).
    std::vector<std::int64_t> nodeRef_;
    std::vector<std::int64_t> nodeQty_;
    std::vector<std::int64_t> nodeSeq_;      // insertion order (priority within level)
    std::vector<int> nodeTick_;              // ladder slot
    std::vector<int> nodePrev_;
    std::vector<int> nodeNext_;
    std::vector<std::uint8_t> nodeSide_;
    int freeHead_;

    // Open-addressing ref -> node map.
    std::vector<std::int64_t> mapKeys_;
    std::vector<int> mapVals_;
    int mapMask_;

    // Own-order tracking (parallel arrays, linear scan -- kMaxTracked is small).
    std::int64_t trackedRef_[kMaxTracked] = {};
    std::int64_t trackedSeq_[kMaxTracked] = {};
    std::int64_t trackedAhead_[kMaxTracked] = {};
    int trackedTick_[kMaxTracked] = {};
    std::uint8_t trackedSide_[kMaxTracked] = {};
    int trackedCount_ = 0;

    ItchCodec::View view_;

    std::int64_t nextSeq_ = 1;
    std::int64_t addCount_ = 0;
    std::int64_t executeCount_ = 0;
    std::int64_t cancelCount_ = 0;
    std::int64_t deleteCount_ = 0;
    std::int64_t replaceCount_ = 0;
    std::int64_t tradeCount_ = 0;
    std::int64_t unknownRefCount_ = 0;
    std::int64_t outOfBandCount_ = 0;
    std::int64_t duplicateRefCount_ = 0;
    int restingOrders_ = 0;
    int lastTradeTick_ = INT32_MIN;
};

}  // namespace quantfinlib
