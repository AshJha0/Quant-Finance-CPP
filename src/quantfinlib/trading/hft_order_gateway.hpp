// Port of com.quantfinlib.trading.HftOrderGateway (Java): ultra-low-latency
// order gateway -- the fast lane from signal to venue. The hot path
// (submit()) performs zero allocation, zero locking, zero map lookups: an
// inline HftRiskGate check over primitive arrays, then a release-store
// publish into a preallocated OrderRingBuffer. A dedicated venue thread
// (busy-spin optional) drains orders to the registered OrderListeners --
// the venue adapter (exchange session, simulator, ...) lives on that side.
//
// Return convention of submit(): positive = accepted order id; negative =
// risk-rejected, with -returnValue the HftRiskGate reason code; 0 = ring
// full (backpressure -- the caller chooses spin, drop, or kill-switch).
//
// Single producer (the trading thread), single consumer (the venue
// thread) -- the same threading model as HftMarketDataBus, and the same
// atomic<shared_ptr<vector<...>>> listener-storage discipline.
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"

namespace quantfinlib {

/** All-primitive order callback on the venue side: no order object, no boxing. */
using OrderListener = std::function<void(std::int64_t orderId, int symbolId, Side side,
                                          std::int64_t quantity, double price,
                                          std::int64_t timestampNanos)>;

class HftOrderGateway {
public:
    HftOrderGateway(int ringCapacity, HftRiskGate& riskGate, bool busySpin);

    /** Parked venue thread, 8K ring. */
    explicit HftOrderGateway(HftRiskGate& riskGate) : HftOrderGateway(1 << 13, riskGate, false) {}

    ~HftOrderGateway() { stop(); }

    HftOrderGateway(const HftOrderGateway&) = delete;
    HftOrderGateway& operator=(const HftOrderGateway&) = delete;

    // ------------------------------------------------------------------
    // Setup (cold path)
    // ------------------------------------------------------------------

    void addOrderListener(OrderListener listener);

    void start();

    /** Stops the venue thread after draining queued orders. */
    void stop();

    void close() { stop(); }

    // ------------------------------------------------------------------
    // Hot path (trading thread)
    // ------------------------------------------------------------------

    /**
     * Risk-checks and submits one order. Zero allocation.
     * @return positive order id if accepted; negative HftRiskGate reason
     *         code if rejected; 0 if the ring is full
     */
    std::int64_t submit(int symbolId, Side side, std::int64_t quantity, double price,
                        std::int64_t timestampNanos);

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    HftRiskGate& riskGate() const { return riskGate_; }

    std::int64_t submittedCount() const { return submitted_; }

    /** Orders handed to the venue thread so far. */
    std::int64_t deliveredCount() const { return delivered_.load(std::memory_order_acquire); }

    std::int64_t ringFullCount() const { return ringFull_; }

private:
    using ListenerVec = std::vector<OrderListener>;
    using ListenerVecPtr = std::shared_ptr<const ListenerVec>;

    void dispatch(std::int64_t orderId, int symbolId, Side side, std::int64_t quantity,
                  double price, std::int64_t timestampNanos);
    void venueLoop();

    OrderRingBuffer ring_;
    HftRiskGate& riskGate_;
    const bool busySpin_;
    std::atomic<ListenerVecPtr> listeners_;

    std::mutex setupMutex_;
    std::atomic<bool> running_{false};
    std::atomic<std::int64_t> delivered_{0};
    std::int64_t submitted_ = 0;   // single-writer (trading thread)
    std::int64_t ringFull_ = 0;    // single-writer (trading thread)
    std::int64_t nextId_ = 1;      // single-writer (trading thread)
    std::thread venueThread_;
};

}  // namespace quantfinlib
