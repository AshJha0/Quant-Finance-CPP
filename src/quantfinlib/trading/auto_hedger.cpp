#include "quantfinlib/trading/auto_hedger.hpp"

#include <cmath>
#include <cstdlib>
#include <stdexcept>

namespace quantfinlib {

AutoHedger::AutoHedger(HftOrderGateway& gateway, int maxSymbols, std::int64_t positionBand,
                       std::int64_t minHedgeIntervalNanos)
        : gateway_(gateway),
          riskGate_(gateway.riskGate()),
          positionBand_(positionBand),
          minHedgeIntervalNanos_(minHedgeIntervalNanos),
          lastHedgeNanos_(static_cast<std::size_t>(maxSymbols), NEVER) {
    if (positionBand <= 0 || minHedgeIntervalNanos < 0) {
        throw std::invalid_argument("positionBand must be > 0 and cooldown >= 0");
    }
}

void AutoHedger::onTick(int symbolId, double price, double /*size*/,
                        std::int64_t timestampNanos) {
    std::int64_t position = riskGate_.position(symbolId);
    std::int64_t excess = std::llabs(position) - positionBand_;
    if (excess <= 0) {
        return;  // inside the band: nothing to do (the overwhelmingly common case)
    }
    auto i = static_cast<std::size_t>(symbolId);
    // Sentinel checked separately: subtracting NEVER would overflow.
    if (lastHedgeNanos_[i] != NEVER && timestampNanos - lastHedgeNanos_[i] < minHedgeIntervalNanos_) {
        return;  // a hedge is already working; wait for its fill
    }
    // Long inventory sells the excess, short buys it back.
    Side side = position > 0 ? Side::SELL : Side::BUY;
    if (gateway_.submit(symbolId, side, excess, price, timestampNanos) > 0) {
        hedgesSubmitted_++;
        lastHedgeNanos_[i] = timestampNanos;
    } else {
        hedgesRejected_++;
    }
}

}  // namespace quantfinlib
