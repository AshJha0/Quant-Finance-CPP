// Port of com.quantfinlib.trading.HftRiskGate (Java): zero-allocation
// pre-trade risk gate for the HFT order path. All state is primitive
// arrays indexed by dense symbol id; check() performs no allocation, no
// hashing and no string formatting, and returns an int reason code.
// Rejection counts are kept per reason for observability.
//
// Threading: the production wiring is inherently multi-threaded --
// check() on the trading/quoting thread, onFill() on the venue-ack
// thread, halt() from ops/dashboards, setReferencePrice() from the
// market-data thread. Cross-thread element access therefore uses
// std::atomic with explicit memory_order_acquire/release, mirroring the
// Java VarHandle discipline 1:1: on x86 an acquire load is a plain load
// and a release store a plain store, so the ~ns-scale check cost is
// unchanged while readers are guaranteed fresh, untorn values. onFill
// uses an atomic add, so multiple fill sources are safe too. Limit
// CONFIGURATION remains setup-time single-threaded.
#pragma once

#include <atomic>
#include <cstdint>
#include <limits>
#include <memory>
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

    explicit HftRiskGate(int maxSymbols);

    // ------------------------------------------------------------------
    // Configuration (cold path)
    // ------------------------------------------------------------------

    HftRiskGate& maxOrderQuantity(std::int64_t qty) {
        maxOrderQuantity_ = qty;
        return *this;
    }

    HftRiskGate& maxOrderNotional(double notional) {
        maxOrderNotional_ = notional;
        return *this;
    }

    HftRiskGate& maxPositionQuantity(std::int64_t qty) {
        maxPositionQuantity_ = qty;
        return *this;
    }

    /** Fat-finger guard versus the reference price (0.02 = 2%). */
    HftRiskGate& priceCollarPct(double pct) {
        priceCollarPct_ = pct;
        return *this;
    }

    /** Halts/unhalts a symbol -- callable from any thread (ops, dashboards). */
    void halt(int symbolId, bool isHalted) {
        halted_[symbolId].store(isHalted, std::memory_order_release);
    }

    /**
     * Gate-wide kill switch: every check rejects with REJECT_KILLED until
     * cleared. Callable from any thread -- this is the lever a cross-shard
     * risk aggregator pulls when firm-wide exposure breaches its cap.
     */
    void kill(bool isKilled) {
        killed_.store(isKilled, std::memory_order_release);
    }

    /** Whether the gate-wide kill switch is currently engaged. */
    bool isKilled() const {
        return killed_.load(std::memory_order_acquire);
    }

    /** The collar reference for a symbol (NaN = unset) -- readable anywhere. */
    double referencePrice(int symbolId) const {
        return referencePrices_[symbolId].load(std::memory_order_acquire);
    }

    /** Symbol capacity this gate was sized for (aggregators iterate to it). */
    int symbolCapacity() const { return maxSymbols_; }

    /** Updates the collar reference (e.g. from the market data bus). */
    void setReferencePrice(int symbolId, double price) {
        referencePrices_[symbolId].store(price, std::memory_order_release);
    }

    // ------------------------------------------------------------------
    // Hot path
    // ------------------------------------------------------------------

    /**
     * Validates one order. Returns OK or a rejection reason code.
     * Zero allocation; acquire loads only (free on x86).
     */
    int check(int symbolId, Side side, std::int64_t quantity, double price);

    /**
     * Applies a fill to the position book -- callable from the venue-ack
     * thread (atomic add: concurrent fill sources cannot lose updates).
     */
    void onFill(int symbolId, Side side, std::int64_t quantity) {
        positions_[symbolId].fetch_add(sign(side) * quantity, std::memory_order_acq_rel);
    }

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    /** Live position -- readable from any thread (quoter skew, hedger, dashboards). */
    std::int64_t position(int symbolId) const {
        return positions_[symbolId].load(std::memory_order_acquire);
    }

    std::int64_t rejectionCount(int reasonCode) const {
        return rejections_[reasonCode].load(std::memory_order_acquire);
    }

    static std::string reasonName(int code);

private:
    static constexpr int kReasons = 7;

    void bumpRejection(int reason) {
        // Atomic add, matching onFill's discipline: two checking threads on
        // one gate must not lose rejection counts (off the reject fast path
        // anyway -- the caller already has its answer).
        rejections_[reason].fetch_add(1, std::memory_order_acq_rel);
    }

    const int maxSymbols_;
    std::unique_ptr<std::atomic<std::int64_t>[]> positions_;   // signed, by symbol id
    std::unique_ptr<std::atomic<double>[]> referencePrices_;   // NaN = no collar check
    std::unique_ptr<std::atomic<bool>[]> halted_;
    std::atomic<std::int64_t> rejections_[kReasons];
    // Gate-wide kill switch on its own cache line: flipped by a firm-level
    // risk aggregator across shards; read with acquire semantics (a plain
    // load on x86) as the very first check.
    alignas(64) std::atomic<bool> killed_{false};
    std::int64_t maxOrderQuantity_ = std::numeric_limits<std::int64_t>::max();
    double maxOrderNotional_ = std::numeric_limits<double>::max();
    std::int64_t maxPositionQuantity_ = std::numeric_limits<std::int64_t>::max();
    double priceCollarPct_ = std::numeric_limits<double>::max();
};

}  // namespace quantfinlib
