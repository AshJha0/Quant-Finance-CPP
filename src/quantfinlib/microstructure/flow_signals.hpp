// Port of com.quantfinlib.microstructure.FlowSignals: streaming order-flow
// signals for short-horizon execution decisions -- order-flow imbalance
// (Cont/Kukanov/Stoikov best-level formulation, exponentially time-decayed),
// queue imbalance, and trade imbalance (time-decayed signed aggressor
// volume over time-decayed total volume). Zero allocation per event.
#pragma once

#include <cstdint>

namespace quantfinlib {

class FlowSignals {
public:
    /** @param halfLifeNanos decay half-life for OFI and trade imbalance. */
    explicit FlowSignals(std::int64_t halfLifeNanos = 500'000'000LL);

    /** Inside-quote update (ticks + sizes); delegates to the double overload. */
    void onQuote(int bidTick, std::int64_t bidSz, int askTick, std::int64_t askSz,
                 std::int64_t timestampNanos);

    /** Inside-quote update on raw double prices -- the cross-asset entry point. */
    void onQuote(double bid, std::int64_t bidSz, double ask, std::int64_t askSz,
                 std::int64_t timestampNanos);

    /** Trade print with aggressor side. */
    void onTrade(bool buyAggressor, std::int64_t quantity, std::int64_t timestampNanos);

    /** Time-decayed net order-flow imbalance in shares (+ = buying pressure). */
    double ofi() const { return ofi_; }

    /** Decay-adjusted OFI as of nowNanos without adding an event. */
    double ofi(std::int64_t nowNanos) const { return decayed(ofi_, ofiTime_, nowNanos); }

    /** Inside-queue imbalance in [-1, 1]; 0 when either side is empty/unset. */
    double queueImbalance() const;

    /** Signed/total decayed aggressor volume in [-1, 1]; 0 before any trade. */
    double tradeImbalance() const;

    std::int64_t quoteCount() const { return quoteCount_; }
    std::int64_t tradeCount() const { return tradeCount_; }

private:
    double decayed(double value, std::int64_t lastTime, std::int64_t now) const;
    double decayFactor(std::int64_t lastTime, std::int64_t now) const;

    double tauNanos_;

    double prevBid_ = 0;
    std::int64_t prevBidSize_ = 0;
    double prevAsk_ = 0;
    std::int64_t prevAskSize_ = 0;
    bool hasQuote_ = false;

    std::int64_t bidSize_ = 0;
    std::int64_t askSize_ = 0;

    double ofi_ = 0;
    std::int64_t ofiTime_ = 0;
    double signedVolume_ = 0;
    double totalVolume_ = 0;
    std::int64_t tradeTime_ = 0;

    std::int64_t quoteCount_ = 0;
    std::int64_t tradeCount_ = 0;
};

}  // namespace quantfinlib
