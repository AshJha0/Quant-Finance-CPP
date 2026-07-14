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
// Listener storage: Java holds per-symbol listener arrays in an
// AtomicReferenceArray<TickListener[]> plus a volatile global array, so
// subscribe() (synchronized, copy-on-write) never blocks the consumer
// thread's plain read. The C++ mirror is
// std::atomic<std::shared_ptr<const std::vector<TickListener>>>: subscribe
// swaps in a new vector under a setup mutex, dispatch() loads the shared_ptr
// with one atomic load (lock-free on libstdc++/x86_64) and the old vector is
// freed only once the last reader (including one mid-dispatch) drops it --
// the C++ analogue of never mutating a published Java array in place.
//
// Dispatch cost note: each subscribed listener is a std::function call;
// with three or more distinct listeners on one symbol this is the
// megamorphic-dispatch cost the Java doc calls out (~10-20 ns per
// listener, no inlining). Keep the per-symbol listener count at one or two
// and fan out inside your own listener when you need more consumers.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
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
    HftMarketDataBus(int ringCapacity, int maxSymbols, bool busySpin);

    /** Parked (non-spinning) consumer with a 64K ring and up to 1024 symbols. */
    HftMarketDataBus() : HftMarketDataBus(1 << 16, 1024, false) {}

    ~HftMarketDataBus() { stop(); }

    HftMarketDataBus(const HftMarketDataBus&) = delete;
    HftMarketDataBus& operator=(const HftMarketDataBus&) = delete;

    // ------------------------------------------------------------------
    // Setup (cold path)
    // ------------------------------------------------------------------

    /** Registers a symbol and returns the dense id used on the hot path. */
    int registerSymbol(const std::string& symbol);

    int symbolId(const std::string& symbol) const { return registry_.id(symbol); }

    std::string symbol(int id) const { return registry_.symbol(id); }

    void subscribe(int symbolId, TickListener listener);

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
     * load).
     */
    bool publish(int symbolId, double price, double size, std::int64_t timestampNanos);

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    /** Last traded price by symbol id; NaN before the first tick. */
    double latestPrice(int symbolId) const;

    double latestPrice(const std::string& symbol) const { return latestPrice(registry_.id(symbol)); }

    std::int64_t processedCount() const { return processed_.load(std::memory_order_acquire); }

    /**
     * Failed publish attempts due to a full ring (backpressure events). A
     * retrying producer increments this per attempt; it does not by itself
     * mean ticks were lost -- that is the caller's backpressure policy.
     */
    std::int64_t ringFullCount() const { return ringFull_; }

private:
    using ListenerVec = std::vector<TickListener>;
    using ListenerVecPtr = std::shared_ptr<const ListenerVec>;

    void dispatch(int symbolId, double price, double size, std::int64_t timestampNanos);
    void consumeLoop();

    SymbolRegistry registry_;
    TickRingBuffer ring_;
    const int maxSymbols_;
    const bool busySpin_;

    std::unique_ptr<std::atomic<double>[]> latestPrice_;
    std::vector<std::atomic<ListenerVecPtr>> symbolListeners_;
    std::atomic<ListenerVecPtr> globalListeners_;

    std::mutex setupMutex_;   // guards subscribe/subscribeAll/start/stop (Java's `synchronized`)
    std::atomic<bool> running_{false};
    // Consumer-thread counter, updated once per drain batch with a release
    // store -- a per-tick store would pay a fence on every dispatch for an
    // observability-only number.
    std::atomic<std::int64_t> processed_{0};
    std::int64_t ringFull_ = 0;   // single-writer (producer thread)
    std::thread consumer_;
};

}  // namespace quantfinlib
