#include "quantfinlib/execution/hft_sor.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {
constexpr std::int32_t NO_BID = std::numeric_limits<std::int32_t>::min();
constexpr std::int32_t NO_ASK = std::numeric_limits<std::int32_t>::max();
}  // namespace

HftSor::HftSor(int venueCount) : venues_(venueCount) {
    if (venueCount < 1) {
        throw std::invalid_argument("venueCount must be >= 1");
    }
    bidTick_.assign(static_cast<std::size_t>(venueCount), NO_BID);
    bidSize_.assign(static_cast<std::size_t>(venueCount), 0);
    askTick_.assign(static_cast<std::size_t>(venueCount), NO_ASK);
    askSize_.assign(static_cast<std::size_t>(venueCount), 0);
    feeTicks_.assign(static_cast<std::size_t>(venueCount), 0.0);
}

void HftSor::fee(int venue, double takerFeeTicks) {
    feeTicks_[static_cast<std::size_t>(venue)] = takerFeeTicks;
}

void HftSor::venueQuote(int venue, std::int32_t bid, std::int64_t bidSz, std::int32_t ask,
                        std::int64_t askSz) {
    auto v = static_cast<std::size_t>(venue);
    bidTick_[v] = bidSz > 0 ? bid : NO_BID;
    bidSize_[v] = bidSz > 0 ? bidSz : 0;
    askTick_[v] = askSz > 0 ? ask : NO_ASK;
    askSize_[v] = askSz > 0 ? askSz : 0;
}

void HftSor::venueDown(int venue) {
    venueQuote(venue, NO_BID, 0, NO_ASK, 0);
}

std::int64_t HftSor::route(Side side, std::int64_t quantity, std::int32_t limitTick,
                           std::int64_t* outQty) {
    std::fill(outQty, outQty + venues_, 0);
    routeCount_++;
    std::int64_t remaining = quantity;
    bool buy = side == Side::BUY;
    while (remaining > 0) {
        int best = -1;
        double bestAllIn = 0;
        for (int v = 0; v < venues_; v++) {
            if (outQty[v] != 0) {
                continue;  // already swept this venue
            }
            std::int32_t px = buy ? askTick_[static_cast<std::size_t>(v)] : bidTick_[static_cast<std::size_t>(v)];
            std::int64_t sz = buy ? askSize_[static_cast<std::size_t>(v)] : bidSize_[static_cast<std::size_t>(v)];
            if (sz <= 0 || px == (buy ? NO_ASK : NO_BID) || (buy ? px > limitTick : px < limitTick)) {
                continue;
            }
            double allIn = buy ? px + feeTicks_[static_cast<std::size_t>(v)] : px - feeTicks_[static_cast<std::size_t>(v)];
            if (best == -1 || (buy ? allIn < bestAllIn : allIn > bestAllIn)) {
                best = v;
                bestAllIn = allIn;
            }
        }
        if (best == -1) {
            break;
        }
        // A selected venue always takes > 0, so outQty doubles as the
        // "already used" marker -- no separate scratch array needed.
        std::int64_t take = std::min(remaining, buy ? askSize_[static_cast<std::size_t>(best)]
                                                    : bidSize_[static_cast<std::size_t>(best)]);
        outQty[best] = take;
        remaining -= take;
    }
    return quantity - remaining;
}

}  // namespace quantfinlib
