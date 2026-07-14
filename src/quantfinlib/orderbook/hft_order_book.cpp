#include "quantfinlib/orderbook/hft_order_book.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include "quantfinlib/orderbook/book_primitives.hpp"

namespace quantfinlib {

namespace bp = book_primitives;

HftOrderBook::HftOrderBook(int minPriceTick, int maxPriceTick, int maxOrders)
        : minTick_(minPriceTick),
          ladder_(maxPriceTick - minPriceTick + 1) {
    if (maxPriceTick < minPriceTick || maxOrders <= 0) {
        throw std::invalid_argument("need maxPriceTick >= minPriceTick, maxOrders > 0");
    }
    bidHead_.assign(ladder_, kNone);
    bidTail_.assign(ladder_, kNone);
    askHead_.assign(ladder_, kNone);
    askTail_.assign(ladder_, kNone);
    bidQty_.assign(ladder_, 0);
    askQty_.assign(ladder_, 0);
    bidBits_.assign((ladder_ + 63) >> 6, 0);
    askBits_.assign((ladder_ + 63) >> 6, 0);

    nodeId_.assign(maxOrders, 0);
    nodeQty_.assign(maxOrders, 0);
    nodeTick_.assign(maxOrders, 0);
    nodePrev_.assign(maxOrders, 0);
    nodeNext_.assign(maxOrders, 0);
    nodeSide_.assign(maxOrders, 0);
    // Thread the free list through nodeNext_.
    for (int i = 0; i < maxOrders - 1; i++) {
        nodeNext_[i] = i + 1;
    }
    nodeNext_[maxOrders - 1] = kNone;
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
    int idx = priceTick - minTick_;
    if (idx < 0 || idx >= ladder_) {
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
    if (quantity <= 0) {
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
    if (quantity <= 0) {
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
    if (quantity <= 0 || !fillableWithin(side, priceTick, quantity)) {
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
    int idx = priceTick - minTick_;
    if (idx < 0 || idx >= ladder_) {
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
            avail += askQty_[idx];
            if (avail >= quantity) {
                return true;
            }
        }
    } else {
        int limit = sellLimitIdx(priceTick);
        for (int idx = bestBidIdx_; idx != kNone && idx >= limit;
             idx = bp::nextSetAtOrBelow(bidBits_.data(), static_cast<int>(bidBits_.size()),
                                        idx - 1)) {
            avail += bidQty_[idx];
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
        remaining = fillLevel(askHead_.data(), askTail_.data(), askQty_.data(),
                              askBits_.data(), bestAskIdx_, takerId, remaining, ts);
        if (askQty_[bestAskIdx_] == 0) {
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
        remaining = fillLevel(bidHead_.data(), bidTail_.data(), bidQty_.data(),
                              bidBits_.data(), bestBidIdx_, takerId, remaining, ts);
        if (bidQty_[bestBidIdx_] == 0) {
            bestBidIdx_ = bp::nextSetAtOrBelow(bidBits_.data(),
                                               static_cast<int>(bidBits_.size()),
                                               bestBidIdx_ - 1);
        }
    }
    return remaining;
}

/** FIFO-fills one level; returns the taker's remaining quantity. */
std::int64_t HftOrderBook::fillLevel(int* head, int* tail, std::int64_t* levelQty,
                                     std::uint64_t* bits, int idx, std::int64_t takerId,
                                     std::int64_t remaining, std::int64_t ts) {
    int node = head[idx];
    while (remaining > 0 && node != kNone) {
        std::int64_t fill = std::min(remaining, nodeQty_[node]);
        remaining -= fill;
        nodeQty_[node] -= fill;
        levelQty[idx] -= fill;
        tradeCount_++;
        if (sink_ != nullptr) {
            sink_(sinkContext_, nodeId_[node], takerId, idx + minTick_, fill, ts);
        }
        if (nodeQty_[node] == 0) {
            // Maker fully filled: pop the head, recycle everything.
            int next = nodeNext_[node];
            head[idx] = next;
            if (next == kNone) {
                tail[idx] = kNone;
            } else {
                nodePrev_[next] = kNone;
            }
            bp::mapRemoveAt(mapKeys_.data(), mapVals_.data(), mapMask_,
                            bp::mapFind(mapKeys_.data(), mapMask_, nodeId_[node]));
            freeNode(node);
            restingOrders_--;
            node = next;
        }
    }
    if (levelQty[idx] == 0) {
        bits[idx >> 6] &= ~(1ULL << (idx & 63));
    }
    return remaining;
}

/** Appends a remainder to its level's FIFO tail and indexes it. */
void HftOrderBook::rest(std::int64_t id, std::uint8_t side, int idx, std::int64_t quantity) {
    int node = freeHead_;
    freeHead_ = nodeNext_[node];
    nodeId_[node] = id;
    nodeQty_[node] = quantity;
    nodeTick_[node] = idx;
    nodeSide_[node] = side;
    nodeNext_[node] = kNone;

    int* head = side == kBuy ? bidHead_.data() : askHead_.data();
    int* tail = side == kBuy ? bidTail_.data() : askTail_.data();
    std::int64_t* qty = side == kBuy ? bidQty_.data() : askQty_.data();
    std::uint64_t* bits = side == kBuy ? bidBits_.data() : askBits_.data();

    int t = tail[idx];
    nodePrev_[node] = t;
    if (t == kNone) {
        head[idx] = node;
    } else {
        nodeNext_[t] = node;
    }
    tail[idx] = node;
    qty[idx] += quantity;
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
    bp::mapPut(mapKeys_.data(), mapVals_.data(), mapMask_, id, node);
    restingOrders_++;
}

/** Removes a (known-resting) node from its level, maintaining best/bitmap. */
void HftOrderBook::unlink(int node) {
    int idx = nodeTick_[node];
    std::uint8_t side = nodeSide_[node];
    int* head = side == kBuy ? bidHead_.data() : askHead_.data();
    int* tail = side == kBuy ? bidTail_.data() : askTail_.data();
    std::int64_t* qty = side == kBuy ? bidQty_.data() : askQty_.data();
    std::uint64_t* bits = side == kBuy ? bidBits_.data() : askBits_.data();

    int prev = nodePrev_[node];
    int next = nodeNext_[node];
    if (prev == kNone) {
        head[idx] = next;
    } else {
        nodeNext_[prev] = next;
    }
    if (next == kNone) {
        tail[idx] = prev;
    } else {
        nodePrev_[next] = prev;
    }
    qty[idx] -= nodeQty_[node];
    if (qty[idx] == 0) {
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

void HftOrderBook::freeNode(int node) {
    nodeNext_[node] = freeHead_;
    freeHead_ = node;
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
    return bestBidIdx_ == kNone ? 0 : bidQty_[bestBidIdx_];
}

std::int64_t HftOrderBook::bestAskSize() const {
    return bestAskIdx_ == kNone ? 0 : askQty_[bestAskIdx_];
}

std::int64_t HftOrderBook::qtyAtTick(Side side, int priceTick) const {
    int idx = priceTick - minTick_;
    if (idx < 0 || idx >= ladder_) {
        return 0;
    }
    return side == Side::BUY ? bidQty_[idx] : askQty_[idx];
}

std::int64_t HftOrderBook::openQuantity(std::int64_t orderId) const {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, orderId);
    return slot == kNone ? 0 : nodeQty_[mapVals_[slot]];
}

int HftOrderBook::snapshot(Side side, int* outTicks, std::int64_t* outQtys,
                           int maxLevels) const {
    int n = 0;
    if (side == Side::BUY) {
        for (int idx = bestBidIdx_; idx != kNone && n < maxLevels;
             idx = bp::nextSetAtOrBelow(bidBits_.data(), static_cast<int>(bidBits_.size()),
                                        idx - 1)) {
            outTicks[n] = idx + minTick_;
            outQtys[n] = bidQty_[idx];
            n++;
        }
    } else {
        for (int idx = bestAskIdx_; idx != kNone && n < maxLevels;
             idx = bp::nextSetAtOrAbove(askBits_.data(), static_cast<int>(askBits_.size()),
                                        idx + 1)) {
            outTicks[n] = idx + minTick_;
            outQtys[n] = askQty_[idx];
            n++;
        }
    }
    return n;
}

}  // namespace quantfinlib
