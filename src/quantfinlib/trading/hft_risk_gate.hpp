// Port of com.quantfinlib.trading.HftRiskGate (Java), hardened to fail
// closed: zero-allocation pre-trade risk gate for the HFT order path. All
// state is primitive arrays indexed by dense symbol id; the hot calls do no
// allocation, hashing or string formatting and return an int reason code.
//
// Fail-closed rules:
//  - a fresh gate rejects everything: every limit starts at zero and the
//    collar is unconfigured. Set the limits, then either priceCollarPct()
//    plus reference prices, or call disableReferenceCheck() on purpose;
//  - symbol id and price are validated before anything else. NaN, infinite,
//    zero and negative prices are rejected;
//  - a missing reference price rejects unless the check was switched off.
//
// Position cap. openBuy and openSell hold quantity reserved by working
// orders, and the cap applies to the worst case on each side:
//     position + openBuy  <= cap        position - openSell >= -cap
// A buy can only raise the first sum, so an order that reduces an over-cap
// short passes and one that flips through zero into a new breach does not,
// with no special case. check() only asks; reserve() asks and takes the
// quantity. HftOrderGateway uses reserve() and gives quantity back on fill,
// cancel and reject.
//
// Kill switch. Each source owns one latched bit. A source clears only its
// own bit, and nothing clears on a timer. While any bit is set, only an
// order that strictly reduces the position (counting reducing orders
// already working) passes. killEpoch() moves on each new trip; gateways
// watch it to run their cancel-all on the venue thread.
//
// Threading: check/reserve on the trading thread, fills on the venue-ack
// thread, halt and kill from ops, setReferencePrice from market data.
// reserve() adds to open first and validates afterwards, so concurrent
// reservers each see the other and the worst outcome is a spurious reject.
// A fill moves position first and releases open second, and readers load
// open before position, so a reader can overstate exposure for an instant
// but never understate it. Limit configuration is setup-time, single thread.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class HftRiskGate {
public:
    static constexpr int OK = 0;
    static constexpr int REJECT_QUANTITY = 1;
    static constexpr int REJECT_NOTIONAL = 2;
    static constexpr int REJECT_POSITION = 3;
    static constexpr int REJECT_PRICE_COLLAR = 4;
    static constexpr int REJECT_HALTED = 5;
    static constexpr int REJECT_KILLED = 6;
    static constexpr int REJECT_PRICE = 7;         // NaN, infinite, zero or negative
    static constexpr int REJECT_SYMBOL = 8;        // id outside [0, capacity)
    static constexpr int REJECT_SELF_TRADE = 9;    // raised by HftOrderGateway
    static constexpr int REJECT_ORDER_TABLE = 10;  // raised by HftOrderGateway
    static constexpr int REJECT_ORDER_STATE = 11;  // raised by HftOrderGateway

    /** Kill sources: one latched bit each. */
    static constexpr std::uint32_t KILL_OPERATOR = 1u;
    static constexpr std::uint32_t KILL_AGGREGATOR = 2u;
    static constexpr std::uint32_t KILL_DEAD_MAN = 4u;
    static constexpr std::uint32_t KILL_GATEWAY = 8u;  // overfill, unsolicited fill, adapter fault

    /** @throws std::invalid_argument if maxSymbols <= 0 */
    explicit HftRiskGate(int maxSymbols);

    // ------------------------------------------------------------------
    // Configuration (cold path). Bad values throw rather than loosen a limit.
    // ------------------------------------------------------------------

    HftRiskGate& maxOrderQuantity(std::int64_t qty) {
        if (qty < 0) throw std::invalid_argument("maxOrderQuantity must be >= 0");
        maxOrderQuantity_ = qty;
        return *this;
    }

    HftRiskGate& maxOrderNotional(double notional) {
        if (!(notional >= 0)) throw std::invalid_argument("maxOrderNotional must be >= 0");
        maxOrderNotional_ = notional;
        return *this;
    }

    HftRiskGate& maxPositionQuantity(std::int64_t qty) {
        if (qty < 0) throw std::invalid_argument("maxPositionQuantity must be >= 0");
        maxPositionQuantity_ = qty;
        return *this;
    }

    /** Fat-finger guard versus the reference price (0.02 = 2%). Turns the check on. */
    HftRiskGate& priceCollarPct(double pct) {
        if (!(pct > 0) || pct > std::numeric_limits<double>::max()) {
            throw std::invalid_argument("priceCollarPct must be finite and > 0");
        }
        priceCollarPct_ = pct;
        referenceCheck_ = true;
        return *this;
    }

    /**
     * Explicit opt-out of the collar and of the reference-price requirement,
     * for flows with no market data behind them. Never the default.
     */
    HftRiskGate& disableReferenceCheck() {
        referenceCheck_ = false;
        return *this;
    }

    /** Halts/unhalts a symbol -- callable from any thread. Bad ids are ignored. */
    void halt(int symbolId, bool isHalted) {
        if (validSymbol(symbolId)) {
            halted_[symbolId].store(isHalted, std::memory_order_release);
        }
    }

    /** Latches one or more kill sources. Callable from any thread. */
    void trip(std::uint32_t sources) {
        std::uint32_t before = killMask_.fetch_or(sources, std::memory_order_acq_rel);
        if ((before | sources) != before) {
            killEpoch_.fetch_add(1, std::memory_order_acq_rel);
        }
    }

    /** Clears only the named sources; any other latched source stays set. */
    void reset(std::uint32_t sources) {
        killMask_.fetch_and(~sources, std::memory_order_acq_rel);
    }

    /** The operator lever: trip or reset KILL_OPERATOR only. */
    void kill(bool isKilled) {
        if (isKilled) {
            trip(KILL_OPERATOR);
        } else {
            reset(KILL_OPERATOR);
        }
    }

    /** Whether any kill source is latched. */
    bool isKilled() const { return killMask() != 0; }

    std::uint32_t killMask() const { return killMask_.load(std::memory_order_acquire); }

    /** Moves on every newly latched source. */
    std::uint64_t killEpoch() const { return killEpoch_.load(std::memory_order_acquire); }

    /** The collar reference for a symbol (NaN = unset or bad id). */
    double referencePrice(int symbolId) const {
        return validSymbol(symbolId) ? referencePrices_[symbolId].load(std::memory_order_acquire)
                                     : std::numeric_limits<double>::quiet_NaN();
    }

    /** Symbol capacity this gate was sized for (aggregators iterate to it). */
    int symbolCapacity() const { return maxSymbols_; }

    /**
     * Updates the collar reference. A price that is not finite and positive
     * is counted and ignored, so one bad tick leaves the last good reference
     * in force; a symbol that never had one stays unpriced and rejects.
     */
    void setReferencePrice(int symbolId, double price);

    /** Drops a reference (stale feed): orders then reject until a new one arrives. */
    void clearReferencePrice(int symbolId) {
        if (validSymbol(symbolId)) {
            referencePrices_[symbolId].store(std::numeric_limits<double>::quiet_NaN(),
                                             std::memory_order_release);
        }
    }

    // ------------------------------------------------------------------
    // Hot path
    // ------------------------------------------------------------------

    /** Validates one order against position plus open; reserves nothing. */
    int check(int symbolId, Side side, std::int64_t quantity, double price);

    /** check() plus an atomic reservation of the quantity when OK. */
    int reserve(int symbolId, Side side, std::int64_t quantity, double price) {
        return reserveDelta(symbolId, side, quantity, quantity, price);
    }

    /**
     * Amend form: orderQuantity and price face the order-level checks, while
     * only `delta` (>= 0) more is reserved against the position cap.
     */
    int reserveDelta(int symbolId, Side side, std::int64_t orderQuantity, std::int64_t delta,
                     double price);

    /** Gives back reserved quantity after a cancel or reject. */
    void releaseOpen(int symbolId, Side side, std::int64_t quantity) {
        if (validSymbol(symbolId) && quantity > 0) {
            openOf(symbolId, side).fetch_sub(quantity, std::memory_order_acq_rel);
        }
    }

    /**
     * Applies a fill with no reservation behind it. Returns false, booking
     * nothing, on a bad symbol id or non-positive quantity.
     */
    bool onFill(int symbolId, Side side, std::int64_t quantity) {
        return onOrderFill(symbolId, side, quantity, 0);
    }

    /** Applies a fill and releases `released` of reservation, in that order. */
    bool onOrderFill(int symbolId, Side side, std::int64_t quantity, std::int64_t released) {
        if (!validSymbol(symbolId) || quantity <= 0) {
            bumpRejection(REJECT_SYMBOL);
            return false;
        }
        positions_[symbolId].fetch_add(sign(side) * quantity, std::memory_order_acq_rel);
        releaseOpen(symbolId, side, released);
        return true;
    }

    /** Counts a rejection raised by a caller on the gate's behalf. */
    void countRejection(int reason) { bumpRejection(reason); }

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    /** Live position -- readable from any thread (0 for a bad id). */
    std::int64_t position(int symbolId) const {
        return validSymbol(symbolId) ? positions_[symbolId].load(std::memory_order_acquire) : 0;
    }

    /** Quantity reserved by working orders on one side (0 for a bad id). */
    std::int64_t openQuantity(int symbolId, Side side) const {
        if (!validSymbol(symbolId)) {
            return 0;
        }
        return (side == Side::BUY ? openBuy_ : openSell_)[symbolId].load(
                std::memory_order_acquire);
    }

    std::int64_t rejectionCount(int reasonCode) const {
        return reasonCode >= 0 && reasonCode < kReasons
                       ? rejections_[reasonCode].load(std::memory_order_acquire)
                       : 0;
    }

    /** Reference updates ignored because the price was unusable. */
    std::int64_t badReferenceCount() const {
        return badReferences_.load(std::memory_order_acquire);
    }

    static std::string reasonName(int code);

private:
    static constexpr int kReasons = 12;

    bool validSymbol(int symbolId) const { return symbolId >= 0 && symbolId < maxSymbols_; }

    std::atomic<std::int64_t>& openOf(int symbolId, Side side) {
        return side == Side::BUY ? openBuy_[symbolId] : openSell_[symbolId];
    }

    /** Every check; position and kill are judged as if delta were already open. */
    int evaluate(int symbolId, Side side, std::int64_t orderQuantity, std::int64_t delta,
                 double price);
    bool withinCap(int symbolId, Side side, std::int64_t open) const;
    bool strictlyReducing(int symbolId, Side side, std::int64_t open) const;

    int reject(int reason) {
        bumpRejection(reason);
        return reason;
    }

    void bumpRejection(int reason) {
        rejections_[reason].fetch_add(1, std::memory_order_acq_rel);
    }

    const int maxSymbols_;
    std::unique_ptr<std::atomic<std::int64_t>[]> positions_;   // signed, by symbol id
    std::unique_ptr<std::atomic<std::int64_t>[]> openBuy_;     // reserved, >= 0
    std::unique_ptr<std::atomic<std::int64_t>[]> openSell_;    // reserved, >= 0
    std::unique_ptr<std::atomic<double>[]> referencePrices_;   // NaN = none
    std::unique_ptr<std::atomic<bool>[]> halted_;
    std::atomic<std::int64_t> rejections_[kReasons];
    std::atomic<std::int64_t> badReferences_{0};
    // Kill state on its own cache line: tripped from other threads, read on
    // every check.
    alignas(64) std::atomic<std::uint32_t> killMask_{0};
    std::atomic<std::uint64_t> killEpoch_{0};
    alignas(64) std::int64_t maxOrderQuantity_ = 0;
    double maxOrderNotional_ = 0;
    std::int64_t maxPositionQuantity_ = 0;
    double priceCollarPct_ = std::numeric_limits<double>::quiet_NaN();   // NaN = unconfigured
    bool referenceCheck_ = true;
};

}  // namespace quantfinlib
