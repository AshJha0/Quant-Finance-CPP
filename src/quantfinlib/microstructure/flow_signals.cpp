#include "quantfinlib/microstructure/flow_signals.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

FlowSignals::FlowSignals(std::int64_t halfLifeNanos) {
    if (halfLifeNanos <= 0) {
        throw std::invalid_argument("halfLifeNanos must be positive");
    }
    tauNanos_ = static_cast<double>(halfLifeNanos) / std::log(2.0);
}

void FlowSignals::onQuote(int bidTick, std::int64_t bidSz, int askTick, std::int64_t askSz,
                          std::int64_t timestampNanos) {
    onQuote(static_cast<double>(bidTick), bidSz, static_cast<double>(askTick), askSz, timestampNanos);
}

void FlowSignals::onQuote(double bid, std::int64_t bidSz, double ask, std::int64_t askSz,
                          std::int64_t timestampNanos) {
    quoteCount_++;
    double inf = std::numeric_limits<double>::infinity();
    if (bidSz <= 0 || askSz <= 0 || !(bid > 0) || !(ask > 0) || bid == inf || ask == inf) {
        bidSize_ = 0;
        askSize_ = 0;
        hasQuote_ = false;
        return;
    }
    bidSize_ = bidSz;
    askSize_ = askSz;
    if (hasQuote_) {
        double e = 0;
        if (bid > prevBid_) {
            e += static_cast<double>(bidSz);
        } else if (bid == prevBid_) {
            e += static_cast<double>(bidSz - prevBidSize_);
        } else {
            e -= static_cast<double>(prevBidSize_);
        }
        if (ask < prevAsk_) {
            e -= static_cast<double>(askSz);
        } else if (ask == prevAsk_) {
            e -= static_cast<double>(askSz - prevAskSize_);
        } else {
            e += static_cast<double>(prevAskSize_);
        }
        ofi_ = decayed(ofi_, ofiTime_, timestampNanos) + e;
        ofiTime_ = timestampNanos;
    } else {
        hasQuote_ = true;
        ofiTime_ = timestampNanos;
    }
    prevBid_ = bid;
    prevBidSize_ = bidSz;
    prevAsk_ = ask;
    prevAskSize_ = askSz;
}

void FlowSignals::onTrade(bool buyAggressor, std::int64_t quantity, std::int64_t timestampNanos) {
    tradeCount_++;
    double decay = decayFactor(tradeTime_, timestampNanos);
    signedVolume_ = signedVolume_ * decay + (buyAggressor ? static_cast<double>(quantity)
                                                           : -static_cast<double>(quantity));
    totalVolume_ = totalVolume_ * decay + static_cast<double>(quantity);
    tradeTime_ = timestampNanos;
}

double FlowSignals::queueImbalance() const {
    if (bidSize_ <= 0 || askSize_ <= 0) {
        return 0;
    }
    return static_cast<double>(bidSize_ - askSize_) / static_cast<double>(bidSize_ + askSize_);
}

double FlowSignals::tradeImbalance() const {
    return totalVolume_ <= 0 ? 0 : signedVolume_ / totalVolume_;
}

double FlowSignals::decayed(double value, std::int64_t lastTime, std::int64_t now) const {
    return value * decayFactor(lastTime, now);
}

double FlowSignals::decayFactor(std::int64_t lastTime, std::int64_t now) const {
    std::int64_t dt = now - lastTime;
    return dt <= 0 ? 1.0 : std::exp(-static_cast<double>(dt) / tauNanos_);
}

}  // namespace quantfinlib
