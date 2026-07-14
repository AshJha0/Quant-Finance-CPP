#include "quantfinlib/marketdata/l3_book_builder.hpp"

#include <algorithm>
#include <bit>
#include <stdexcept>

#include "quantfinlib/orderbook/book_primitives.hpp"

namespace quantfinlib {

namespace bp = book_primitives;

namespace {
std::vector<int> filled(int n) {
    return std::vector<int>(n, -1);
}
}  // namespace

L3BookBuilder::L3BookBuilder(int stockLocate, int minPriceTick, int maxPriceTick, int maxOrders)
        : stockLocate_(stockLocate),
          minTick_(minPriceTick),
          ladder_(maxPriceTick - minPriceTick + 1) {
    if (maxPriceTick < minPriceTick || maxOrders <= 0) {
        throw std::invalid_argument("need maxPriceTick >= minPriceTick, maxOrders > 0");
    }
    bidHead_ = filled(ladder_);
    bidTail_ = filled(ladder_);
    askHead_ = filled(ladder_);
    askTail_ = filled(ladder_);
    bidQty_.assign(ladder_, 0);
    askQty_.assign(ladder_, 0);
    bidBits_.assign((ladder_ + 63) >> 6, 0);
    askBits_.assign((ladder_ + 63) >> 6, 0);

    nodeRef_.assign(maxOrders, 0);
    nodeQty_.assign(maxOrders, 0);
    nodeSeq_.assign(maxOrders, 0);
    nodeTick_.assign(maxOrders, 0);
    nodePrev_.assign(maxOrders, 0);
    nodeNext_.assign(maxOrders, 0);
    nodeSide_.assign(maxOrders, 0);
    for (int i = 0; i < maxOrders - 1; i++) {
        nodeNext_[i] = i + 1;
    }
    nodeNext_[maxOrders - 1] = kNone;
    freeHead_ = 0;

    int cap = static_cast<int>(
            std::bit_floor(static_cast<unsigned>(std::max(16, maxOrders * 2 - 1))) << 1);
    mapKeys_.assign(cap, 0);
    mapVals_.assign(cap, 0);
    mapMask_ = cap - 1;
}

// ------------------------------------------------------------------
// Feed entry points
// ------------------------------------------------------------------

int L3BookBuilder::onMessage(const unsigned char* buf, int offset) {
    ItchCodec::View& v = view_.wrap(buf, offset);
    int len = ItchCodec::length(v.type());
    if (len < 0 || v.stockLocate() != stockLocate_) {
        return 0;
    }
    switch (v.type()) {
        case ItchCodec::ADD:
        case ItchCodec::ADD_MPID:
            onAdd(v.orderRef(), v.side() == ItchCodec::BUY ? Side::BUY : Side::SELL, v.shares(),
                  v.priceTick());
            break;
        case ItchCodec::EXECUTED:
            onExecute(v.orderRef(), v.deltaShares());
            break;
        case ItchCodec::CANCEL:
            onCancel(v.orderRef(), v.deltaShares());
            break;
        case ItchCodec::DELETE:
            onDelete(v.orderRef());
            break;
        case ItchCodec::REPLACE:
            onReplace(v.origRef(), v.newRef(), v.shares(), v.priceTick());
            break;
        case ItchCodec::TRADE:
            onTrade(v.priceTick());
            break;
        default:
            return 0;
    }
    return len;
}

bool L3BookBuilder::onAdd(std::int64_t ref, Side side, std::int64_t shares, int priceTick) {
    if (!insert(ref, side, shares, priceTick)) {
        return false;
    }
    addCount_++;
    return true;
}

bool L3BookBuilder::insert(std::int64_t ref, Side side, std::int64_t shares, int priceTick) {
    int idx = priceTick - minTick_;
    if (idx < 0 || idx >= ladder_ || shares <= 0 || ref <= 0 || freeHead_ == kNone) {
        outOfBandCount_++;
        return false;
    }
    if (bp::mapFind(mapKeys_.data(), mapMask_, ref) != kNone) {
        // Re-delivered add (gap-recovery replay / simulator bug): a blind
        // insert would leave a phantom second node the venue's future
        // delete can never remove.
        duplicateRefCount_++;
        return false;
    }
    std::uint8_t s = side == Side::BUY ? kBuy : kSell;
    int node = freeHead_;
    freeHead_ = nodeNext_[node];
    nodeRef_[node] = ref;
    nodeQty_[node] = shares;
    nodeSeq_[node] = nextSeq_++;
    nodeTick_[node] = idx;
    nodeSide_[node] = s;
    nodeNext_[node] = kNone;

    std::vector<int>& head = s == kBuy ? bidHead_ : askHead_;
    std::vector<int>& tail = s == kBuy ? bidTail_ : askTail_;
    std::vector<std::int64_t>& qty = s == kBuy ? bidQty_ : askQty_;
    std::vector<std::uint64_t>& bits = s == kBuy ? bidBits_ : askBits_;

    int t = tail[idx];
    nodePrev_[node] = t;
    if (t == kNone) {
        head[idx] = node;
    } else {
        nodeNext_[t] = node;
    }
    tail[idx] = node;
    qty[idx] += shares;
    bits[idx >> 6] |= 1ULL << (idx & 63);
    if (s == kBuy) {
        if (bestBidIdx_ == kNone || idx > bestBidIdx_) {
            bestBidIdx_ = idx;
        }
    } else {
        if (bestAskIdx_ == kNone || idx < bestAskIdx_) {
            bestAskIdx_ = idx;
        }
    }
    bp::mapPut(mapKeys_.data(), mapVals_.data(), mapMask_, ref, node);
    restingOrders_++;
    return true;
}

void L3BookBuilder::onExecute(std::int64_t ref, std::int64_t shares) {
    if (reduce(ref, shares, false)) {
        executeCount_++;
    } else {
        unknownRefCount_++;
    }
}

void L3BookBuilder::onCancel(std::int64_t ref, std::int64_t shares) {
    if (reduce(ref, shares, true)) {
        cancelCount_++;
    } else {
        unknownRefCount_++;
    }
}

void L3BookBuilder::onDelete(std::int64_t ref) {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, ref);
    if (slot == kNone) {
        unknownRefCount_++;
        return;
    }
    deleteCount_++;
    removeWhole(mapVals_[slot], slot);
}

void L3BookBuilder::onReplace(std::int64_t origRef, std::int64_t newRef, std::int64_t shares,
                              int priceTick) {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, origRef);
    if (slot == kNone) {
        unknownRefCount_++;
        return;
    }
    replaceCount_++;
    int node = mapVals_[slot];
    Side side = nodeSide_[node] == kBuy ? Side::BUY : Side::SELL;
    removeWhole(node, slot);
    insert(newRef, side, shares, priceTick);
}

bool L3BookBuilder::reduce(std::int64_t ref, std::int64_t shares, bool bySeq) {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, ref);
    if (slot == kNone) {
        return false;
    }
    int node = mapVals_[slot];
    std::int64_t cut = std::min(shares, nodeQty_[node]);
    creditAhead(node, cut, bySeq);
    nodeQty_[node] -= cut;
    levelQtyOf(node)[nodeTick_[node]] -= cut;
    if (nodeQty_[node] == 0) {
        removeNode(node, slot);
    }
    return true;
}

void L3BookBuilder::removeWhole(int node, int slot) {
    creditAhead(node, nodeQty_[node], true);
    levelQtyOf(node)[nodeTick_[node]] -= nodeQty_[node];
    removeNode(node, slot);
}

void L3BookBuilder::onTrade(int priceTick) {
    tradeCount_++;
    lastTradeTick_ = priceTick;
}

// ------------------------------------------------------------------
// Own-order queue tracking
// ------------------------------------------------------------------

bool L3BookBuilder::track(std::int64_t ref) {
    for (int i = 0; i < trackedCount_; i++) {
        if (trackedRef_[i] == ref) {
            return true;                 // idempotent: a retried ack must
        }                                // not create a leaking duplicate row
    }
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, ref);
    if (slot == kNone || trackedCount_ == kMaxTracked) {
        return false;
    }
    int node = mapVals_[slot];
    std::int64_t ahead = 0;
    int idx = nodeTick_[node];
    int cur = (nodeSide_[node] == kBuy ? bidHead_ : askHead_)[idx];
    while (cur != node && cur != kNone) {
        ahead += nodeQty_[cur];
        cur = nodeNext_[cur];
    }
    trackedRef_[trackedCount_] = ref;
    trackedSeq_[trackedCount_] = nodeSeq_[node];
    trackedAhead_[trackedCount_] = ahead;
    trackedTick_[trackedCount_] = idx;
    trackedSide_[trackedCount_] = nodeSide_[node];
    trackedCount_++;
    return true;
}

void L3BookBuilder::untrack(std::int64_t ref) {
    for (int i = 0; i < trackedCount_; i++) {
        if (trackedRef_[i] == ref) {
            trackedCount_--;
            trackedRef_[i] = trackedRef_[trackedCount_];
            trackedSeq_[i] = trackedSeq_[trackedCount_];
            trackedAhead_[i] = trackedAhead_[trackedCount_];
            trackedTick_[i] = trackedTick_[trackedCount_];
            trackedSide_[i] = trackedSide_[trackedCount_];
            return;
        }
    }
}

std::int64_t L3BookBuilder::sharesAhead(std::int64_t ref) const {
    for (int i = 0; i < trackedCount_; i++) {
        if (trackedRef_[i] == ref) {
            return trackedAhead_[i];
        }
    }
    return -1;
}

void L3BookBuilder::creditAhead(int node, std::int64_t qty, bool bySeq) {
    int idx = nodeTick_[node];
    std::uint8_t side = nodeSide_[node];
    std::int64_t seq = nodeSeq_[node];
    std::int64_t ref = nodeRef_[node];
    for (int i = 0; i < trackedCount_; i++) {
        if (trackedTick_[i] == idx && trackedSide_[i] == side && trackedRef_[i] != ref
                && (!bySeq || seq < trackedSeq_[i])) {
            trackedAhead_[i] = std::max<std::int64_t>(0, trackedAhead_[i] - qty);
        }
    }
}

// ------------------------------------------------------------------
// Node/level plumbing
// ------------------------------------------------------------------

std::vector<std::int64_t>& L3BookBuilder::levelQtyOf(int node) {
    return nodeSide_[node] == kBuy ? bidQty_ : askQty_;
}

void L3BookBuilder::removeNode(int node, int mapSlot) {
    int idx = nodeTick_[node];
    std::uint8_t side = nodeSide_[node];
    std::vector<int>& head = side == kBuy ? bidHead_ : askHead_;
    std::vector<int>& tail = side == kBuy ? bidTail_ : askTail_;

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
    untrack(nodeRef_[node]);              // ends tracking if this was ours
    bp::mapRemoveAt(mapKeys_.data(), mapVals_.data(), mapMask_, mapSlot);
    nodeNext_[node] = freeHead_;
    freeHead_ = node;
    restingOrders_--;

    std::vector<std::int64_t>& qty = side == kBuy ? bidQty_ : askQty_;
    if (qty[idx] == 0) {
        std::vector<std::uint64_t>& bits = side == kBuy ? bidBits_ : askBits_;
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

// ------------------------------------------------------------------
// Queries
// ------------------------------------------------------------------

int L3BookBuilder::bestBidTick() const {
    return bestBidIdx_ == kNone ? INT32_MIN : bestBidIdx_ + minTick_;
}

int L3BookBuilder::bestAskTick() const {
    return bestAskIdx_ == kNone ? INT32_MAX : bestAskIdx_ + minTick_;
}

std::int64_t L3BookBuilder::bestBidSize() const {
    return bestBidIdx_ == kNone ? 0 : bidQty_[bestBidIdx_];
}

std::int64_t L3BookBuilder::bestAskSize() const {
    return bestAskIdx_ == kNone ? 0 : askQty_[bestAskIdx_];
}

std::int64_t L3BookBuilder::qtyAtTick(Side side, int priceTick) const {
    int idx = priceTick - minTick_;
    if (idx < 0 || idx >= ladder_) {
        return 0;
    }
    return side == Side::BUY ? bidQty_[idx] : askQty_[idx];
}

std::int64_t L3BookBuilder::openQuantity(std::int64_t ref) const {
    int slot = bp::mapFind(mapKeys_.data(), mapMask_, ref);
    return slot == kNone ? 0 : nodeQty_[mapVals_[slot]];
}

int L3BookBuilder::snapshot(Side side, int* outTicks, std::int64_t* outQtys, int maxLevels) const {
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
