#include "quantfinlib/trading/hft_risk_gate.hpp"

#include <cmath>
#include <cstdlib>

namespace quantfinlib {

HftRiskGate::HftRiskGate(int maxSymbols)
        : maxSymbols_(maxSymbols),
          positions_(new std::atomic<std::int64_t>[maxSymbols]),
          referencePrices_(new std::atomic<double>[maxSymbols]),
          halted_(new std::atomic<bool>[maxSymbols]) {
    for (int i = 0; i < maxSymbols; i++) {
        positions_[i].store(0, std::memory_order_relaxed);
        referencePrices_[i].store(std::numeric_limits<double>::quiet_NaN(),
                                  std::memory_order_relaxed);
        halted_[i].store(false, std::memory_order_relaxed);
    }
    for (auto& r : rejections_) {
        r.store(0, std::memory_order_relaxed);
    }
}

int HftRiskGate::check(int symbolId, Side side, std::int64_t quantity, double price) {
    if (killed_.load(std::memory_order_acquire)) {
        bumpRejection(REJECT_KILLED);
        return REJECT_KILLED;
    }
    if (halted_[symbolId].load(std::memory_order_acquire)) {
        bumpRejection(REJECT_HALTED);
        return REJECT_HALTED;
    }
    if (quantity <= 0 || quantity > maxOrderQuantity_) {
        bumpRejection(REJECT_QUANTITY);
        return REJECT_QUANTITY;
    }
    if (static_cast<double>(quantity) * price > maxOrderNotional_) {
        bumpRejection(REJECT_NOTIONAL);
        return REJECT_NOTIONAL;
    }
    std::int64_t current = positions_[symbolId].load(std::memory_order_acquire);
    std::int64_t newPosition = current + sign(side) * quantity;
    // Risk-REDUCING orders pass the position check: two 800-lot fills
    // against a 1000 cap leave the book at 1600, and a gate that then
    // rejects a 500-lot reducing order (|1100| > cap) is refusing the
    // exact trade that de-risks it. An over-cap projection is allowed
    // only when it BOTH shrinks the absolute position AND keeps its
    // sign -- a "reduce" that flips +1600 through zero to -1400 is a
    // brand-new over-cap short wearing a hedge's clothes.
    if (std::llabs(newPosition) > maxPositionQuantity_
            && (std::llabs(newPosition) >= std::llabs(current)
                    || (newPosition ^ current) < 0)) {
        bumpRejection(REJECT_POSITION);
        return REJECT_POSITION;
    }
    double ref = referencePrices_[symbolId].load(std::memory_order_acquire);
    if (ref == ref && priceCollarPct_ != std::numeric_limits<double>::max()) {  // NaN-safe
        double deviation = std::fabs(price - ref);
        if (deviation > ref * priceCollarPct_) {
            bumpRejection(REJECT_PRICE_COLLAR);
            return REJECT_PRICE_COLLAR;
        }
    }
    return OK;
}

std::string HftRiskGate::reasonName(int code) {
    switch (code) {
        case OK: return "OK";
        case REJECT_QUANTITY: return "MAX_ORDER_QTY";
        case REJECT_NOTIONAL: return "MAX_NOTIONAL";
        case REJECT_POSITION: return "MAX_POSITION";
        case REJECT_PRICE_COLLAR: return "PRICE_COLLAR";
        case REJECT_HALTED: return "HALTED";
        case REJECT_KILLED: return "KILLED";
        default: return "UNKNOWN(" + std::to_string(code) + ")";
    }
}

}  // namespace quantfinlib
