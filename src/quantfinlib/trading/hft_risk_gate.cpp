#include "quantfinlib/trading/hft_risk_gate.hpp"

#include <cmath>
#include <cstdlib>

namespace quantfinlib {

namespace {

constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

// b >= 0 throughout: a position plus an open quantity must not wrap.
std::int64_t saturatingAdd(std::int64_t a, std::int64_t b) { return a > kMax - b ? kMax : a + b; }
std::int64_t saturatingSub(std::int64_t a, std::int64_t b) { return a < kMin + b ? kMin : a - b; }

bool usablePrice(double price) {
    return price > 0 && price <= std::numeric_limits<double>::max();   // false on NaN
}

int checkedSize(int maxSymbols) {
    if (maxSymbols <= 0) {
        throw std::invalid_argument("maxSymbols must be > 0");
    }
    return maxSymbols;
}

}  // namespace

HftRiskGate::HftRiskGate(int maxSymbols)
        : maxSymbols_(checkedSize(maxSymbols)),
          symbols_(new SymbolState[static_cast<std::size_t>(maxSymbols)]) {
    for (auto& r : rejections_) {
        r.store(0, std::memory_order_relaxed);
    }
}

void HftRiskGate::setReferencePrice(int symbolId, double price) {
    if (!validSymbol(symbolId) || !usablePrice(price)) {
        badReferences_.fetch_add(1, std::memory_order_acq_rel);
        return;
    }
    symbols_[symbolId].reference.store(price, std::memory_order_release);
}

bool HftRiskGate::withinCap(int symbolId, Side side, std::int64_t open) const {
    // The caller loaded `open` before this position load; see the header.
    std::int64_t position = symbols_[symbolId].position.load(std::memory_order_acquire);
    return side == Side::BUY ? saturatingAdd(position, open) <= maxPositionQuantity_
                             : saturatingSub(position, open) >= -maxPositionQuantity_;
}

bool HftRiskGate::strictlyReducing(int symbolId, Side side, std::int64_t open) const {
    std::int64_t position = symbols_[symbolId].position.load(std::memory_order_acquire);
    // The buy form avoids negating INT64_MIN.
    return side == Side::BUY ? position < 0 && open - 1 <= -(position + 1)
                             : position > 0 && open <= position;
}

int HftRiskGate::evaluate(int symbolId, Side side, std::int64_t orderQuantity, std::int64_t delta,
                          double price) {
    if (!validSymbol(symbolId)) {
        return reject(REJECT_SYMBOL);
    }
    if (!usablePrice(price)) {
        return reject(REJECT_PRICE);
    }
    std::int64_t open = saturatingAdd(openOf(symbolId, side).load(std::memory_order_acquire),
                                      delta > 0 ? delta : 0);
    if (killMask_.load(std::memory_order_acquire) != 0
            && (orderQuantity <= 0 || !strictlyReducing(symbolId, side, open))) {
        return reject(REJECT_KILLED);
    }
    if (symbols_[symbolId].halted.load(std::memory_order_acquire)) {
        return reject(REJECT_HALTED);
    }
    if (orderQuantity <= 0 || orderQuantity > maxOrderQuantity_ || delta < 0) {
        return reject(REJECT_QUANTITY);
    }
    if (!(static_cast<double>(orderQuantity) * price <= maxOrderNotional_)) {
        return reject(REJECT_NOTIONAL);
    }
    if (referenceCheck_) {
        double ref = symbols_[symbolId].reference.load(std::memory_order_acquire);
        // Written so that a NaN reference or an unconfigured collar rejects.
        if (!(std::fabs(price - ref) <= ref * priceCollarPct_)) {
            return reject(REJECT_PRICE_COLLAR);
        }
    }
    if (delta > 0 && !withinCap(symbolId, side, open)) {
        return reject(REJECT_POSITION);
    }
    return OK;
}

int HftRiskGate::check(int symbolId, Side side, std::int64_t quantity, double price) {
    return evaluate(symbolId, side, quantity, quantity, price);
}

int HftRiskGate::reserveDelta(int symbolId, Side side, std::int64_t orderQuantity,
                              std::int64_t delta, double price) {
    int code = evaluate(symbolId, side, orderQuantity, delta, price);
    if (code != OK || delta == 0) {
        return code;
    }
    // Take first, validate after: see the threading note in the header.
    std::atomic<std::int64_t>& openRef = openOf(symbolId, side);
    std::int64_t open = openRef.fetch_add(delta, std::memory_order_acq_rel) + delta;
    if (!withinCap(symbolId, side, open)) {
        openRef.fetch_sub(delta, std::memory_order_acq_rel);
        return reject(REJECT_POSITION);
    }
    if (killMask_.load(std::memory_order_acquire) != 0
            && !strictlyReducing(symbolId, side, open)) {
        openRef.fetch_sub(delta, std::memory_order_acq_rel);
        return reject(REJECT_KILLED);
    }
    return OK;
}

HftRiskGate::Layout HftRiskGate::layout() const {
    auto base = reinterpret_cast<std::uintptr_t>(this);
    auto at = [base](const void* p) {
        return static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(p) - base);
    };
    const SymbolState& s = symbols_[0];
    return Layout{sizeof(SymbolState),
                  alignof(SymbolState),
                  static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(&s.reference)
                                           - reinterpret_cast<std::uintptr_t>(&s.position)),
                  at(&symbols_),
                  at(&rejections_[0]),
                  at(&killMask_),
                  at(&maxOrderQuantity_)};
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
        case REJECT_PRICE: return "INVALID_PRICE";
        case REJECT_SYMBOL: return "UNKNOWN_SYMBOL";
        case REJECT_SELF_TRADE: return "SELF_TRADE";
        case REJECT_ORDER_TABLE: return "ORDER_TABLE_FULL";
        case REJECT_ORDER_STATE: return "ORDER_STATE";
        default: return "UNKNOWN(" + std::to_string(code) + ")";
    }
}

}  // namespace quantfinlib
