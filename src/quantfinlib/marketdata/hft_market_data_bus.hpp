// Port of com.quantfinlib.marketdata.HftMarketDataBus (Java): ultra-low-
// latency market data bus. The hot path -- publish() through each
// TickListener -- performs zero allocation, zero locking and zero map
// lookups:
//
//  - Ticks travel through a preallocated primitive TickRingBuffer.
//  - Symbols are dense int ids (SymbolRegistry); listener dispatch and the
//    last-price cache are plain array indexing.
//  - The consumer thread can busy-spin (a PAUSE-instruction spin, the
//    Thread.onSpinWait() analogue) for minimum hand-off latency, or park
//    (sleep_for) when latency matters less than CPU.
//
// Listener storage: each symbol slot (and the global slot) is one
// std::atomic<const ListenerList*>. The list behind the pointer is immutable
// once published, so dispatch() is a single acquire load of a raw pointer,
// which is lock-free on every target we build for. (This replaced
// std::atomic<std::shared_ptr>, which is mutex-backed in libstdc++.)
// subscribe() builds a new list under a setup mutex and swaps it in. The old
// list may still be in use by a dispatch in flight, so it is retired, not
// freed: retired lists are released by stop() after the consumer has joined,
// and by the destructor. A bus that keeps subscribing while running holds
// one small list per subscribe until then; subscribing is a setup action.
//
// Bad input on the hot path: publish() returns false for a symbolId outside
// [0, maxSymbols) and counts it, so a bad id never reaches an array index.
//
// Listener exceptions: a listener that throws is counted and skipped for
// that tick. The remaining listeners still run, the consumer keeps draining,
// and the process does not terminate. Watch listenerErrorCount().
//
// Dispatch cost note: each subscribed listener is a std::function call;
// with three or more distinct listeners on one symbol this is the
// megamorphic-dispatch cost the Java doc calls out (~10-20 ns per
// listener, no inlining). Keep the per-symbol listener count at one or two
// and fan out inside your own listener when you need more consumers.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "quantfinlib/marketdata/symbol_registry.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"

namespace quantfinlib {

/** All-primitive tick callback: no event object, no boxing. Must be fast and non-blocking. */
using TickListener = std::function<void(int symbolId, double price, double size,
                                         std::int64_t timestampNanos)>;

class HftMarketDataBus {
public:
    /** @throws std::invalid_argument when maxSymbols <= 0 or the ring is too large. */
    HftMarketDataBus(int ringCapacity, int maxSymbols, bool busySpin);

    /** Parked (non-spinning) consumer with a 64K ring and up to 1024 symbols. */
    HftMarketDataBus() : HftMarketDataBus(1 << 16, 1024, false) {}

    ~HftMarketDataBus();

    HftMarketDataBus(const HftMarketDataBus&) = delete;
    HftMarketDataBus& operator=(const HftMarketDataBus&) = delete;

    // ------------------------------------------------------------------
    // Setup (cold path)
    // ------------------------------------------------------------------

    /** Registers a symbol and returns the dense id used on the hot path. */
    int registerSymbol(const std::string& symbol);

    int symbolId(const std::string& symbol) const { return registry_.id(symbol); }

    /** @throws std::out_of_range for an id that was never registered. */
    std::string symbol(int id) const {
        if (id < 0 || id >= registry_.size()) {
            throw std::out_of_range("symbol id " + std::to_string(id) + " not registered");
        }
        return registry_.symbol(id);
    }

    /** @throws std::out_of_range on a bad symbolId, std::invalid_argument on an empty listener. */
    void subscribe(int symbolId, TickListener listener);

    /** @throws std::invalid_argument on an empty listener. */
    void subscribeAll(TickListener listener);

    void start();

    /** Stops the consumer after draining queued ticks. */
    void stop();

    void close() { stop(); }

    // ------------------------------------------------------------------
    // Hot path
    // ------------------------------------------------------------------

    /**
     * Publishes a tick; single producer thread, zero allocation. Returns
     * false when the ring is full (caller decides: spin, drop, or shed
     * load) or when symbolId is outside [0, maxSymbols) (never retry that;
     * see invalidSymbolCount()).
     */
    bool publish(int symbolId, double price, double size, std::int64_t timestampNanos);

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    /** Last traded price by symbol id; NaN before the first tick or for a bad id. */
    double latestPrice(int symbolId) const;

    double latestPrice(const std::string& symbol) const { return latestPrice(registry_.id(symbol)); }

    std::int64_t processedCount() const { return processed_.load(std::memory_order_acquire); }

    /**
     * Failed publish attempts due to a full ring (backpressure events). A
     * retrying producer increments this per attempt; it does not by itself
     * mean ticks were lost -- that is the caller's backpressure policy.
     */
    std::int64_t ringFullCount() const { return ringFull_.load(std::memory_order_relaxed); }

    /** publish() calls refused because symbolId was out of range. */
    std::int64_t invalidSymbolCount() const {
        return invalidSymbol_.load(std::memory_order_relaxed);
    }

    /** Listener invocations that ended in an exception. */
    std::int64_t listenerErrorCount() const {
        return listenerErrors_.load(std::memory_order_relaxed);
    }

    /** True when dispatch's listener load cannot fall back to a lock. */
    static constexpr bool listenerLoadIsLockFree() {
        return std::atomic<const std::vector<const TickListener*>*>::is_always_lock_free;
    }

private:
    using ListenerList = std::vector<const TickListener*>;

    void dispatch(int symbolId, double price, double size, std::int64_t timestampNanos);
    void invoke(const ListenerList& list, int symbolId, double price, double size,
                std::int64_t timestampNanos);
    void consumeLoop();
    void addListener(std::atomic<const ListenerList*>& slot, TickListener listener);
    bool validSymbol(int symbolId) const { return symbolId >= 0 && symbolId < maxSymbols_; }

    SymbolRegistry registry_;
    TickRingBuffer ring_;
    const int maxSymbols_;
    const bool busySpin_;

    std::unique_ptr<std::atomic<double>[]> latestPrice_;
    std::vector<std::atomic<const ListenerList*>> symbolListeners_;
    std::atomic<const ListenerList*> globalListeners_{nullptr};

    std::mutex lifecycleMutex_;   // serializes start/stop, so two consumers never overlap
    std::mutex setupMutex_;       // guards listeners_ and retired_
    std::deque<TickListener> listeners_;   // owns every listener; deque keeps addresses stable
    std::vector<std::unique_ptr<const ListenerList>> retired_;

    std::atomic<bool> running_{false};
    // Consumer-thread counter, updated once per drain batch with a release
    // store -- a per-tick store would pay a fence on every dispatch for an
    // observability-only number.
    std::atomic<std::int64_t> processed_{0};
    // Single-writer counters read from other threads: relaxed load + store
    // compile to plain moves, with no locked instruction on the hot path.
    std::atomic<std::int64_t> ringFull_{0};        // producer thread
    std::atomic<std::int64_t> invalidSymbol_{0};   // producer thread
    std::atomic<std::int64_t> listenerErrors_{0};  // consumer thread
    std::thread consumer_;
};

}  // namespace quantfinlib
