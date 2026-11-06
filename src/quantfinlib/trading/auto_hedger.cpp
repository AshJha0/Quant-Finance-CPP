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
          lastHedgeNanos_(static_cast<std::size_t>(maxSymbols), NEVER),
          hedgeOrder_(static_cast<std::size_t>(maxSymbols)),
          hedgeSide_(static_cast<std::size_t>(maxSymbols), Side::BUY),
          hedgePrice_(static_cast<std::size_t>(maxSymbols)) {
    if (positionBand <= 0 || minHedgeIntervalNanos < 0) {
        throw std::invalid_argument("positionBand must be > 0 and cooldown >= 0");
    }
}

void AutoHedger::onTick(int symbolId, double price, double /*size*/,
                        std::int64_t timestampNanos) {
    if (symbolId < 0 || static_cast<std::size_t>(symbolId) >= hedgeOrder_.size()) {
        return;
    }
    auto i = static_cast<std::size_t>(symbolId);
    std::int64_t position = riskGate_.position(symbolId);
    std::int64_t excess = std::llabs(position) - positionBand_;
    if (excess <= 0) {
        // Inside the band: nothing to do (the overwhelmingly common case),
        // unless a hedge is still out and would now overshoot.
        if (hedgeOrder_[i] != 0
                && (gateway_.leavesQuantity(hedgeOrder_[i]) == 0
                    || gateway_.cancel(hedgeOrder_[i], timestampNanos) > 0)) {
            hedgeOrder_[i] = 0;
        }
        return;
    }
    // Sentinel checked separately: subtracting NEVER would overflow.
    bool coolingDown = lastHedgeNanos_[i] != NEVER
                       && timestampNanos - lastHedgeNanos_[i] < minHedgeIntervalNanos_;
    // Long inventory sells the excess, short buys it back.
    Side side = position > 0 ? Side::SELL : Side::BUY;

    std::int64_t working = hedgeOrder_[i] != 0 ? gateway_.leavesQuantity(hedgeOrder_[i]) : 0;
    if (working > 0 && hedgeSide_[i] != side) {
        // Inventory crossed over: the old hedge now adds to the problem.
        gateway_.cancel(hedgeOrder_[i], timestampNanos);
        return;
    }
    if (working > 0) {
        if (working >= excess && (coolingDown || price == hedgePrice_[i])) {
            hedgesCovered_++;
            return;
        }
        std::int64_t result = gateway_.replace(hedgeOrder_[i], excess, price, timestampNanos);
        if (result > 0) {
            hedgesAmended_++;
            lastHedgeNanos_[i] = timestampNanos;
            hedgePrice_[i] = price;
        } else if (result == -HftRiskGate::REJECT_ORDER_STATE) {
            hedgesCovered_++;   // unacked or amend pending: still in flight
        } else {
            hedgesRejected_++;
        }
        return;
    }
    if (coolingDown) {
        return;
    }
    std::int64_t id = gateway_.submit(symbolId, side, excess, price, timestampNanos);
    if (id > 0) {
        hedgesSubmitted_++;
        lastHedgeNanos_[i] = timestampNanos;
        hedgeOrder_[i] = id;
        hedgeSide_[i] = side;
        hedgePrice_[i] = price;
    } else {
        hedgeOrder_[i] = 0;
        hedgesRejected_++;
    }
}

}  // namespace quantfinlib
