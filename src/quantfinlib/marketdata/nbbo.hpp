// Port of com.quantfinlib.marketdata.Nbbo (Java): National Best Bid and
// Offer -- aggregates per-venue top-of-book quotes for one symbol into the
// consolidated best bid/ask, the size available at those prices, and a
// bitmask of which venues are at the inside -- the three inputs a smart
// order router actually consumes. The equities counterpart of
// fx::AggregatedBook, in integer ticks.
//
// Zero allocation per update; single-writer (one consolidated-feed
// thread). Recomputation is a linear scan over venues -- with the <= 64
// venues of a real consolidated tape, a branch-predictable scan over four
// parallel arrays beats any incremental structure.
//
// An optional Listener fires only when the NBBO actually changes (price
// or size at the inside), so downstream logic is naturally conflated to
// inside-quote updates. The listener is a raw function pointer + context
// (Java's @FunctionalInterface primitive callback), matching
// HftOrderBook::TradeSink so installing/invoking never allocates.
#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace quantfinlib {

class Nbbo {
public:
    static constexpr int NO_BID = INT32_MIN;
    static constexpr int NO_ASK = INT32_MAX;

    /** Fired after the NBBO (price or inside size) changes. Primitive-only. */
    using Listener = void (*)(void* context, int bidTick, std::int64_t bidSize, int askTick,
                              std::int64_t askSize, std::int64_t timestampNanos);

    /** @param venueCount number of venues (<= 64, so venue sets fit one bitmask) */
    explicit Nbbo(int venueCount) : venues_(venueCount) {
        if (venueCount < 1 || venueCount > 64) {
            throw std::invalid_argument("venueCount must be 1..64");
        }
        bidTick_.assign(venueCount, NO_BID);
        bidSize_.assign(venueCount, 0);
        askTick_.assign(venueCount, NO_ASK);
        askSize_.assign(venueCount, 0);
    }

    /** Installs the (single) inside-change callback. */
    void listener(Listener l, void* context) {
        listener_ = l;
        listenerContext_ = context;
    }

    /**
     * One venue's new top of book. Pass NO_BID/NO_ASK (or zero sizes) for
     * an empty side; use onVenueDown when the venue drops entirely.
     * Returns true when the NBBO changed.
     */
    bool onVenueQuote(int venue, int bid, std::int64_t bidSz, int ask, std::int64_t askSz,
                       std::int64_t timestampNanos) {
        int newBid = bidSz > 0 ? bid : NO_BID;
        int newAsk = askSz > 0 ? ask : NO_ASK;
        bidTick_[venue] = newBid;
        bidSize_[venue] = bidSz > 0 ? bidSz : 0;
        askTick_[venue] = newAsk;
        askSize_[venue] = askSz > 0 ? askSz : 0;
        updateCount_++;
        // Fast path: a venue that was not at either inside and stays
        // strictly off it cannot move the NBBO -- most consolidated-tape
        // updates are exactly this off-inside flicker, so skip the scan
        // for them. Provably no change iff: not previously at either
        // inside (bit clear covers "its old quote WAS the inside"), and
        // the new bid stays strictly below the inside (or absent), and
        // the new ask strictly above (or absent). Any comparison against
        // an absent inside (NO_BID/NO_ASK sentinel) is false, correctly
        // forcing the scan.
        std::uint64_t bit = 1ULL << venue;
        if ((nbbVenueBits_ & bit) == 0 && (nboVenueBits_ & bit) == 0
                && (newBid == NO_BID || newBid < nbb_) && (newAsk == NO_ASK || newAsk > nbo_)) {
            return false;
        }
        return recompute(timestampNanos);
    }

    /** Removes a venue's quotes (feed loss / venue halt). Returns true on NBBO change. */
    bool onVenueDown(int venue, std::int64_t timestampNanos) {
        // One state-clearing path: an empty quote and a downed venue must
        // never drift apart.
        return onVenueQuote(venue, NO_BID, 0, NO_ASK, 0, timestampNanos);
    }

    /** National best bid in ticks; NO_BID when no venue bids. */
    int bidTick() const { return nbb_; }

    /** Total displayed size at the national best bid, across venues. */
    std::int64_t bidSize() const { return nbbSize_; }

    /** National best offer in ticks; NO_ASK when no venue offers. */
    int askTick() const { return nbo_; }

    /** Total displayed size at the national best offer, across venues. */
    std::int64_t askSize() const { return nboSize_; }

    /** Bitmask of venues quoting at the national best bid (bit v = venue v). */
    std::uint64_t bidVenues() const { return nbbVenueBits_; }

    /** Bitmask of venues quoting at the national best offer. */
    std::uint64_t askVenues() const { return nboVenueBits_; }

    /** Crossed market flag (NBB above NBO -- locked/crossed tape condition). */
    bool crossed() const { return nbb_ != NO_BID && nbo_ != NO_ASK && nbb_ > nbo_; }

    /** Locked market flag (NBB equal to NBO). */
    bool locked() const { return nbb_ != NO_BID && nbb_ == nbo_; }

    /** Mid in tick units, NaN when either side is absent. */
    double midTick() const {
        // Sum in int64: two ticks near 2^30 (a ~$107k symbol in 0.0001
        // ticks) would wrap a 32-bit sum negative.
        if (nbb_ == NO_BID || nbo_ == NO_ASK) {
            return std::numeric_limits<double>::quiet_NaN();
        }
        return (static_cast<std::int64_t>(nbb_) + nbo_) / 2.0;
    }

    std::int64_t updateCount() const { return updateCount_; }

    /** Updates that moved the inside (price or size) -- the conflation ratio. */
    std::int64_t changeCount() const { return changeCount_; }

private:
    bool recompute(std::int64_t ts) {
        int bb = NO_BID;
        std::int64_t bbSz = 0;
        std::uint64_t bbBits = 0;
        int bo = NO_ASK;
        std::int64_t boSz = 0;
        std::uint64_t boBits = 0;
        for (int v = 0; v < venues_; v++) {
            int b = bidTick_[v];
            if (b != NO_BID) {
                if (b > bb) {
                    bb = b;
                    bbSz = bidSize_[v];
                    bbBits = 1ULL << v;
                } else if (b == bb) {
                    bbSz += bidSize_[v];
                    bbBits |= 1ULL << v;
                }
            }
            int a = askTick_[v];
            if (a != NO_ASK) {
                if (a < bo) {
                    bo = a;
                    boSz = askSize_[v];
                    boBits = 1ULL << v;
                } else if (a == bo) {
                    boSz += askSize_[v];
                    boBits |= 1ULL << v;
                }
            }
        }
        bool changed = bb != nbb_ || bbSz != nbbSize_ || bo != nbo_ || boSz != nboSize_;
        nbb_ = bb;
        nbbSize_ = bbSz;
        nbbVenueBits_ = bbBits;
        nbo_ = bo;
        nboSize_ = boSz;
        nboVenueBits_ = boBits;
        if (changed) {
            changeCount_++;
            if (listener_ != nullptr) {
                listener_(listenerContext_, bb, bbSz, bo, boSz, ts);
            }
        }
        return changed;
    }

    int venues_;
    std::vector<int> bidTick_;
    std::vector<std::int64_t> bidSize_;
    std::vector<int> askTick_;
    std::vector<std::int64_t> askSize_;

    int nbb_ = NO_BID;
    std::int64_t nbbSize_ = 0;
    std::uint64_t nbbVenueBits_ = 0;
    int nbo_ = NO_ASK;
    std::int64_t nboSize_ = 0;
    std::uint64_t nboVenueBits_ = 0;

    Listener listener_ = nullptr;
    void* listenerContext_ = nullptr;
    std::int64_t updateCount_ = 0;
    std::int64_t changeCount_ = 0;
};

}  // namespace quantfinlib
