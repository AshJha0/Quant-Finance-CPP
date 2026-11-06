#include "quantfinlib/orderbook/hft_order_book.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <stdexcept>

#include "quantfinlib/orderbook/book_primitives.hpp"

namespace quantfinlib {

namespace bp = book_primitives;

namespace {

/** Validated before any member is sized; the span is formed in 64 bits. */
int checkedLadder(int minPriceTick, int maxPriceTick, int maxOrders) {
    std::int64_t ladder = static_cast<std::int64_t>(maxPriceTick) - minPriceTick + 1;
    if (ladder <= 0 || ladder > HftOrderBook::MAX_LADDER || maxOrders <= 0
            || minPriceTick == HftOrderBook::NO_BID || maxPriceTick == HftOrderBook::NO_ASK
            || maxOrders > HftOrderBook::MAX_ORDERS) {
        throw std::invalid_argument(
                "need maxPriceTick >= minPriceTick, a ladder within MAX_LADDER, ticks clear "
                "of the NO_BID/NO_ASK sentinels and 0 < maxOrders <= MAX_ORDERS");
    }
    return static_cast<int>(ladder);
}

}  // namespace

HftOrderBook::HftOrderBook(int minPriceTick, int maxPriceTick, int maxOrders)
        : minTick_(minPriceTick),
          ladder_(checkedLadder(minPriceTick, maxPriceTick, maxOrders)),
          maxOrderQuantity_(std::numeric_limits<std::int64_t>::max() / maxOrders) {
    bids_.assign(ladder_, Level{0, kNone, kNone});
    asks_.assign(ladder_, Level{0, kNone, kNone});
    bidBits_.assign((ladder_ + 63) >> 6, 0);
    askBits_.assign((ladder_ + 63) >> 6, 0);

    nodes_.assign(maxOrders, Node{});
    // Thread the free list through Node::next.
    for (int i = 0; i < maxOrders - 1; i++) {
        nodes_[i].next = i + 1;
    }
    nodes_[maxOrders - 1].next = kNone;
    freeHead_ = 0;

    // 2x capacity, power of two: max load factor 0.5 keeps probes short
    // (Java: Integer.highestOneBit(Math.max(16, maxOrders * 2 - 1)) << 1).
    int cap = static_cast<int>(
            std::bit_floor(static_cast<unsigned>(std::max(16, maxOrders * 2 - 1))) << 1);
    mapKeys_.assign(cap, 0);
    mapVals_.assign(cap, 0);
    mapMask_ = cap - 1;
}

void HftOrderBook::tradeSink(TradeSink sink, void* context) {
    sink_ = sink;
    sinkContext_ = context;
}

// ------------------------------------------------------------------
// Order entry (the hot path)
// ------------------------------------------------------------------

std::int64_t HftOrderBook::submitLimit(Side side, int priceTick, std::int64_t quantity,
                                       std::int64_t timestampNanos) {
    if (quantity <= 0) {
        return REJECT_INVALID;
    }
    if (quantity > maxOrderQuantity_) {
        return REJECT_QUANTITY_LIMIT;
    }
    int idx = ladderIdx(priceTick);
    if (idx == kNone) {
        return REJECT_OUT_OF_BAND;
    }
    std::int64_t id = nextId_++;
    orderCount_++;
    std::int64_t remaining = side == Side::BUY
                                     ? matchBuy(id, idx, quantity, timestampNanos)
                                     : matchSell(id, idx, quantity, timestampNanos);
    if (remaining > 0) {
        if (freeHead_ == kNone) {
            // Pool exhausted: the matched portion stands (trades were
            // real); only the resting remainder is rejected -- exactly
            // how a venue sheds load without unwinding executions.
            return REJECT_POOL_FULL;
        }
        rest(id, side == Side::BUY ? kBuy : kSell, idx, remaining);
    }
    return id;
}

std::int64_t HftOrderBook::submitMarket(Side side, std::int64_t quantity,
                                        std::int64_t timestampNanos) {
    if (!validQuantity(quantity)) {
        return 0;
    }
    std::int64_t id = nextId_++;
    orderCount_++;
    std::int64_t remaining = side == Side::BUY
                                     ? matchBuy(id, ladder_ - 1, quantity, timestampNanos)
                                     : matchSell(id, 0, quantity, timestampNanos);
    return quantity - remaining;
}

std::int64_t HftOrderBook::submitIoc(Side side, int priceTick, std::int64_t quantity,
                                     std::int64_t timestampNanos) {
    if (!validQuantity(quantity)) {
        return 0;
    }
    std::int64_t id = nextId_++;
    orderCount_++;
    // Clamp only the aggressive end: an off-band-aggressive limit is a
    // market order; an off-band-passive limit simply cannot cross. The
    // subtraction is done in 64-bit so sentinel limits (INT32_MIN/MAX for
    // "pure market") cannot wrap into the opposite meaning.
    std::int64_t remaining =
            side == Side::BUY
                    ? matchBuy(id, buyLimitIdx(priceTick), quantity, timestampNanos)
                    : matchSell(id, sellLimitIdx(priceTick), quantity, timestampNanos);
    return quantity - remaining;
}

int HftOrderBook::ladderIdx(int priceTick) const {
    std::int64_t d = static_cast<std::int64_t>(priceTick) - minTick_;
    return d < 0 || d >= ladder_ ? kNone : static_cast<int>(d);
}

/** Buy limit as a ladder index, overflow-safe: [-1 = uncrossable, ladder-1 = market]. */
int HftOrderBook::buyLimitIdx(int priceTick) const {
    std::int64_t d = static_cast<std::int64_t>(priceTick) - minTick_;
    return static_cast<int>(
            std::max(std::min(d, static_cast<std::int64_t>(ladder_) - 1),
                     static_cast<std::int64_t>(-1)));
}

/** Sell limit as a ladder index, overflow-safe: [0 = market, ladder = uncrossable]. */
int HftOrderBook::sellLimitIdx(int priceTick) const {
    std::int64_t d = static_cast<std::int64_t>(priceTick) - minTick_;
    return static_cast<int>(
            std::min(std::max(d, static_cast<std::int64_t>(0)),
                     static_cast<std::int64_t>(ladder_)));
}

std::int64_t HftOrderBook::submitFok(Side side, int priceTick, std::int64_t quantity,
                                     std::int64_t timestampNanos) {
    if (!validQuantity(quantity) || !fillableWithin(side, priceTick, quantity)) {
        return 0;
    }
    return submitIoc(side, priceTick, quantity, timestampNanos);
}

std::int64_t HftOrderBook::submitPostOnly(Side side, int priceTick, std::int64_t quantity,
                                          std::int64_t timestampNanos) {
    (void)timestampNanos;
    if (quantity <= 0) {
        return REJECT_INVALID;
    }
    if (quantity > maxOrderQuantity_) {
        return REJECT_QUANTITY_LIMIT;
    }
    int idx = ladderIdx(priceTick);
    if (idx == kNone) {
        return REJECT_OUT_OF_BAND;
    }
    bool crosses = side == Side::BUY
                           ? bestAskIdx_ != kNone && bestAskIdx_ <= idx
                           : bestBidIdx_ != kNone && bestBidIdx_ >= idx;
    if (crosses) {
        return REJECT_WOULD_CROSS;
    }
    // Same id/count discipline as submitLimit: a pool-full reject still
    // consumed an id and counts as an order, so id sequences and
    // orderCount reconcile identically regardless of entry point.
    std::int64_t id = nextId_++;
    orderCount_++;
    if (freeHead_ == kNone) {
        return REJECT_POOL_FULL;
    }
    rest(id, side == Side::BUY ? kBuy : kSell, idx, quantity);
    return id;
}

/** True when the opposite side offers at least `quantity` within the limit. */
bool HftOrderBook::fillableWithin(Side side, int priceTick, std::int64_t quantity) const {
    std::int64_t avail = 0;
    if (side == Side::BUY) {
        int limit = buyLimitIdx(priceTick);
        for (int idx = bestAskIdx_; idx != kNone && idx <= limit;
             idx = bp::nextSetAtOrAbove(askBits_.data(), static_cast<int>(askBits_.size()),
                                        idx + 1)) {
            avail += asks_[idx].qty;
            if (avail >= quantity) {
                return true;
            }
        }
    } else {
        int limit = sellLimitIdx(priceTick);
        for (int idx = bestBidIdx_; idx != kNone && idx >= limit;
             idx = bp::nextSetAtOrBelow(bidBits_.data(), static_cast<int>(bidBits_.size()),
                                        idx - 1)) {
            avail += bids_[idx].qty;
            if (avail >= quantity) {
                return true;
            }
        }
    }
    return false;
}

bool HftOrderBook::cancel(std::int64_t orderId) {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, orderId);
    if (slot == kNone) {
        return false;
    }
    int node = mapVals_[slot];
    unlink(node);
    bp::mapRemoveAt(mapKeys_.data(), mapVals_.data(), mapMask_, slot);
    freeNode(node);
    cancelCount_++;
    restingOrders_--;
    return true;
}

// ------------------------------------------------------------------
// Matching (private hot path)
// ------------------------------------------------------------------

/** Buy taker: lift asks from the lowest occupied level up to limitIdx. */
std::int64_t HftOrderBook::matchBuy(std::int64_t takerId, int limitIdx, std::int64_t quantity,
                                    std::int64_t ts) {
    std::int64_t remaining = quantity;
    while (remaining > 0 && bestAskIdx_ != kNone && bestAskIdx_ <= limitIdx) {
        remaining = fillLevel(asks_[bestAskIdx_], askBits_.data(), bestAskIdx_, takerId,
                              remaining, ts);
        if (asks_[bestAskIdx_].qty == 0) {
            bestAskIdx_ = bp::nextSetAtOrAbove(askBits_.data(),
                                               static_cast<int>(askBits_.size()),
                                               bestAskIdx_ + 1);
        }
    }
    return remaining;
}

/** Sell taker: hit bids from the highest occupied level down to limitIdx. */
std::int64_t HftOrderBook::matchSell(std::int64_t takerId, int limitIdx, std::int64_t quantity,
                                     std::int64_t ts) {
    std::int64_t remaining = quantity;
    while (remaining > 0 && bestBidIdx_ != kNone && bestBidIdx_ >= limitIdx) {
        remaining = fillLevel(bids_[bestBidIdx_], bidBits_.data(), bestBidIdx_, takerId,
                              remaining, ts);
        if (bids_[bestBidIdx_].qty == 0) {
            bestBidIdx_ = bp::nextSetAtOrBelow(bidBits_.data(),
                                               static_cast<int>(bidBits_.size()),
                                               bestBidIdx_ - 1);
        }
    }
    return remaining;
}

/** FIFO-fills one level; returns the taker's remaining quantity. */
std::int64_t HftOrderBook::fillLevel(Level& level, std::uint64_t* bits, int idx,
                                     std::int64_t takerId, std::int64_t remaining,
                                     std::int64_t ts) {
    int handle = level.head;
    while (remaining > 0 && handle != kNone) {
        Node& node = nodes_[handle];
        std::int64_t fill = std::min(remaining, node.qty);
        remaining -= fill;
        node.qty -= fill;
        level.qty -= fill;
        tradeCount_++;
        if (sink_ != nullptr) {
            sink_(sinkContext_, node.id, takerId, idx + minTick_, fill, ts);
        }
        if (node.qty == 0) {
            // Maker fully filled: pop the head, recycle everything.
            int next = node.next;
            level.head = next;
            if (next == kNone) {
                level.tail = kNone;
            } else {
                nodes_[next].prev = kNone;
            }
            bp::mapRemoveAt(mapKeys_.data(), mapVals_.data(), mapMask_,
                            bp::mapFind(mapKeys_.data(), mapMask_, node.id));
            freeNode(handle);
            restingOrders_--;
            handle = next;
        }
    }
    if (level.qty == 0) {
        bits[idx >> 6] &= ~(1ULL << (idx & 63));
    }
    return remaining;
}

/** Appends a remainder to its level's FIFO tail and indexes it. */
void HftOrderBook::rest(std::int64_t id, std::uint8_t side, int idx, std::int64_t quantity) {
    int handle = freeHead_;
    Node& node = nodes_[handle];
    freeHead_ = node.next;
    Level& level = side == kBuy ? bids_[idx] : asks_[idx];
    std::uint64_t* bits = side == kBuy ? bidBits_.data() : askBits_.data();

    int t = level.tail;
    node = Node{id, quantity, idx, t, kNone, side};
    if (t == kNone) {
        level.head = handle;
    } else {
        nodes_[t].next = handle;
    }
    level.tail = handle;
    level.qty += quantity;
    bits[idx >> 6] |= 1ULL << (idx & 63);

    if (side == kBuy) {
        if (bestBidIdx_ == kNone || idx > bestBidIdx_) {
            bestBidIdx_ = idx;
        }
    } else {
        if (bestAskIdx_ == kNone || idx < bestAskIdx_) {
            bestAskIdx_ = idx;
        }
    }
    bp::mapPut(mapKeys_.data(), mapVals_.data(), mapMask_, id, handle);
    restingOrders_++;
}

/** Removes a (known-resting) node from its level, maintaining best/bitmap. */
void HftOrderBook::unlink(int handle) {
    const Node& node = nodes_[handle];
    int idx = node.tick;
    std::uint8_t side = node.side;
    Level& level = side == kBuy ? bids_[idx] : asks_[idx];
    std::uint64_t* bits = side == kBuy ? bidBits_.data() : askBits_.data();

    if (node.prev == kNone) {
        level.head = node.next;
    } else {
        nodes_[node.prev].next = node.next;
    }
    if (node.next == kNone) {
        level.tail = node.prev;
    } else {
        nodes_[node.next].prev = node.prev;
    }
    level.qty -= node.qty;
    if (level.qty == 0) {
        bits[idx >> 6] &= ~(1ULL << (idx & 63));
        if (side == kBuy && idx == bestBidIdx_) {
            bestBidIdx_ = bp::nextSetAtOrBelow(bidBits_.data(),
                                               static_cast<int>(bidBits_.size()), idx - 1);
        } else if (side == kSell && idx == bestAskIdx_) {
            bestAskIdx_ = bp::nextSetAtOrAbove(askBits_.data(),
                                               static_cast<int>(askBits_.size()), idx + 1);
        }
    }
}

void HftOrderBook::freeNode(int handle) {
    nodes_[handle].next = freeHead_;
    freeHead_ = handle;
}

// ------------------------------------------------------------------
// Queries (all primitive, zero allocation)
// ------------------------------------------------------------------

int HftOrderBook::bestBidTick() const {
    return bestBidIdx_ == kNone ? NO_BID : bestBidIdx_ + minTick_;
}

int HftOrderBook::bestAskTick() const {
    return bestAskIdx_ == kNone ? NO_ASK : bestAskIdx_ + minTick_;
}

std::int64_t HftOrderBook::bestBidSize() const {
    return bestBidIdx_ == kNone ? 0 : bids_[bestBidIdx_].qty;
}

std::int64_t HftOrderBook::bestAskSize() const {
    return bestAskIdx_ == kNone ? 0 : asks_[bestAskIdx_].qty;
}

std::int64_t HftOrderBook::qtyAtTick(Side side, int priceTick) const {
    int idx = ladderIdx(priceTick);
    if (idx == kNone) {
        return 0;
    }
    return side == Side::BUY ? bids_[idx].qty : asks_[idx].qty;
}

std::int64_t HftOrderBook::openQuantity(std::int64_t orderId) const {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, orderId);
    return slot == kNone ? 0 : nodes_[mapVals_[slot]].qty;
}

int HftOrderBook::snapshot(Side side, int* outTicks, std::int64_t* outQtys,
                           int maxLevels) const {
    int n = 0;
    if (side == Side::BUY) {
        for (int idx = bestBidIdx_; idx != kNone && n < maxLevels;
             idx = bp::nextSetAtOrBelow(bidBits_.data(), static_cast<int>(bidBits_.size()),
                                        idx - 1)) {
            outTicks[n] = idx + minTick_;
            outQtys[n] = bids_[idx].qty;
            n++;
        }
    } else {
        for (int idx = bestAskIdx_; idx != kNone && n < maxLevels;
             idx = bp::nextSetAtOrAbove(askBits_.data(), static_cast<int>(askBits_.size()),
                                        idx + 1)) {
            outTicks[n] = idx + minTick_;
            outQtys[n] = asks_[idx].qty;
            n++;
        }
    }
    return n;
}

}  // namespace quantfinlib
