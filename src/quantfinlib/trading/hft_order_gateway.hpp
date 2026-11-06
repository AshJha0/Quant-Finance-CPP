// Port of com.quantfinlib.trading.HftOrderGateway (Java), extended into an
// order state machine: the fast lane from signal to venue. submit() reserves
// through HftRiskGate, takes a slot in a preallocated order table and
// publishes into a preallocated OrderRingBuffer. A venue thread drains the
// ring to the listeners; the venue adapter reports back through the on*
// methods below. Nothing here allocates after construction.
//
// Return convention of submit/cancel/replace: positive = accepted; negative
// = rejected, with -returnValue an HftRiskGate reason code; 0 = ring full
// (backpressure -- the caller chooses spin, drop, or kill).
//
// Identifiers. submit() returns the order id, which stays the handle for the
// order's whole life. Each wire message carries a ClOrdID:
//     clOrdId = orderId | version << 40
// version 0 is the new order, so its ClOrdID equals the order id; every
// cancel or replace takes the next version and names the last ClOrdID the
// venue accepted as origClOrdId, as FIX does: a refused request never renames
// the order. A report quoting any issued version finds the
// order: a fill in flight may still carry the old one.
//
// Life of an order:
//   QUEUED  in the ring, not sent. A cancel or kill ends it here; the venue
//           never hears of it.
//   SENT    delivered to the venue, not acked. May be cancelled, not replaced.
//   LIVE    acked. May be cancelled or replaced; one request pending at a time.
//   done    filled, cancelled or rejected: reservation released, slot freed.
//
// Reservation = quantity that can still fill. A pending replace holds
// max(old, new) less what has filled, because until the venue answers either
// could apply. The quantity in a replace message is the FIX total, including
// what has already filled.
//
// Execution faults. A fill is a fact, so it always reaches the position, even
// when it should not exist. An overfill, a fill for an unknown or finished
// order, a fill whose symbol or side disagree with the order, or a malformed
// report latches KILL_GATEWAY for an operator to look at.
//
// Kill. On a new trip (HftRiskGate::killEpoch) the venue thread sweeps the
// table: queued orders stop there and working orders get cancels. Reduce-only
// orders accepted during the kill are exempt. An order queued before the kill
// cannot reach the venue, because delivery re-checks under the lock.
//
// Dead man. enableDeadMan() trips KILL_DEAD_MAN if heartbeat() goes quiet for
// the timeout. The venue thread checks it against the steady clock.
//
// Self-trade prevention. An order that would cross a working order of ours on
// the same symbol is rejected (cancel-newest) unless a hook allows it. A
// per-symbol price bound, loose but never too tight, keeps the common case to
// one compare; only an order inside the bound walks the symbol's working list.
//
// Threading. Any thread may call submit, cancel, replace and the on* methods:
// one spinlock guards the table and serialises ring producers. It is never
// held across a listener, so a listener may call back in. It cannot go: the
// reservation, the self-trade check and the ring publish must be one step
// against a table the ack thread also changes. The usual hold is a few dozen
// instructions. Two holds are longer, by design and bounded: the self-trade
// walk, only when a price lands inside the bound, is as long as the symbol's
// working list; a sweep takes the lock per block of slots, so a kill never
// stalls the trading thread for a whole table.
//
// Listeners live behind a plain atomic pointer to an immutable list, as in
// HftMarketDataBus. Every list ever published is kept until the destructor,
// since a drain may still be walking an old one. Adding is a setup action.
#pragma once

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"

namespace quantfinlib {

/** All-primitive new-order callback on the venue side. */
using OrderListener = std::function<void(std::int64_t orderId, int symbolId, Side side,
                                          std::int64_t quantity, double price,
                                          std::int64_t timestampNanos)>;

/** One outbound wire message. origClOrdId is 0 on a new order. */
struct VenueMessage {
    enum class Type : std::uint8_t { NEW = 0, CANCEL = 1, REPLACE = 2 };
    Type type;
    std::int64_t clOrdId;
    std::int64_t origClOrdId;
    int symbolId;
    Side side;
    std::int64_t quantity;
    double price;
    std::int64_t timestampNanos;
};

/** Every outbound message: what a real venue adapter registers. */
using VenueListener = std::function<void(const VenueMessage&)>;

class HftOrderGateway {
public:
    /** What a self-trade hook sees: the incoming order and the order it crosses. */
    struct SelfTradeCandidate {
        int symbolId;
        Side side;
        std::int64_t quantity;
        double price;
        std::int64_t restingOrderId;
        double restingPrice;
    };

    /** Returns true to let the cross through. Called under the table lock: keep it short. */
    using SelfTradeHook = bool (*)(void* context, const SelfTradeCandidate& candidate);

    /** @param maxOpenOrders order-table size, rounded up to a power of two */
    HftOrderGateway(int ringCapacity, HftRiskGate& riskGate, bool busySpin, int maxOpenOrders);

    /** Table sized at twice the ring, and at least 4096. */
    HftOrderGateway(int ringCapacity, HftRiskGate& riskGate, bool busySpin)
            : HftOrderGateway(ringCapacity, riskGate, busySpin,
                              ringCapacity * 2 > 4096 ? ringCapacity * 2 : 4096) {}

    /** Parked venue thread, 8K ring. */
    explicit HftOrderGateway(HftRiskGate& riskGate) : HftOrderGateway(1 << 13, riskGate, false) {}

    ~HftOrderGateway() { stop(); }

    HftOrderGateway(const HftOrderGateway&) = delete;
    HftOrderGateway& operator=(const HftOrderGateway&) = delete;

    // ------------------------------------------------------------------
    // Setup (cold path)
    // ------------------------------------------------------------------

    /** Receives new orders only. A real adapter needs addVenueListener. */
    void addOrderListener(OrderListener listener);

    void addVenueListener(VenueListener listener);

    /** Replaces the default reject-on-cross policy. Set before trading starts. */
    void setSelfTradeHook(SelfTradeHook hook, void* context) {
        selfTradeHook_ = hook;
        selfTradeContext_ = context;
    }

    /** Arms the dead man now; timeoutNanos <= 0 disarms it. */
    void enableDeadMan(std::int64_t timeoutNanos, std::int64_t nowNanos = steadyNanos());

    void start();

    /** Stops the venue thread after draining queued messages. */
    void stop();

    void close() { stop(); }

    // ------------------------------------------------------------------
    // Hot path (trading side)
    // ------------------------------------------------------------------

    /** Risk-checks, reserves and queues one order. Returns the order id. */
    std::int64_t submit(int symbolId, Side side, std::int64_t quantity, double price,
                        std::int64_t timestampNanos);

    /**
     * Requests a cancel. A queued order ends at once. Allowed while killed.
     * @return positive on success, -REJECT_ORDER_STATE if unknown, finished
     *         or already being cancelled, 0 if the ring is full
     */
    std::int64_t cancel(std::int64_t orderId, std::int64_t timestampNanos);

    /**
     * Cancel-replace of a LIVE order. newLeaves is the quantity to have
     * working after the amend. Side cannot change. Runs the full gate checks.
     * @return the new ClOrdID; -REJECT_ORDER_STATE if not LIVE or a request
     *         is pending; another negative reason; 0 if the ring is full
     */
    std::int64_t replace(std::int64_t orderId, std::int64_t newLeaves, double newPrice,
                         std::int64_t timestampNanos);

    /**
     * Cancels every working order. Callable from any thread. Returns the
     * orders acted on; on a full ring the venue thread finishes the job.
     */
    int cancelAll(std::int64_t timestampNanos);

    /** Strategy liveness signal, in the steadyNanos() clock. */
    void heartbeat(std::int64_t nowNanos = steadyNanos()) {
        lastHeartbeat_.store(nowNanos, std::memory_order_release);
    }

    /** Trips KILL_DEAD_MAN if the heartbeat is older than the timeout. */
    bool checkDeadMan(std::int64_t nowNanos);

    static std::int64_t steadyNanos();

    // ------------------------------------------------------------------
    // Venue reports. Each returns false if it matched no order or state.
    // ------------------------------------------------------------------

    bool onOrderAck(std::int64_t clOrdId);
    bool onOrderReject(std::int64_t clOrdId);
    bool onCancelAck(std::int64_t clOrdId);
    bool onCancelReject(std::int64_t clOrdId);
    bool onReplaceAck(std::int64_t clOrdId);
    bool onReplaceReject(std::int64_t clOrdId);

    /**
     * The venue says the order is over without a pending request of ours
     * behind it: an unsolicited cancel, an expiry, its own cancel-on-disconnect.
     * Also the right call for a solicited cancel confirmation.
     */
    bool onOrderOut(std::int64_t clOrdId);

    /** Books a fill. False means an execution fault: see the header note. */
    bool onFill(std::int64_t clOrdId, int symbolId, Side side, std::int64_t quantity);

    /** One venue-thread pass, for a gateway driven without start(). */
    int pollOnce();

    // ------------------------------------------------------------------
    // Observability
    // ------------------------------------------------------------------

    HftRiskGate& riskGate() const { return riskGate_; }

    /** Quantity still able to fill on the order at its current size; 0 once done. */
    std::int64_t leavesQuantity(std::int64_t orderId) const;

    /** Quantity filled so far on the order; -1 if it is not in the table. */
    std::int64_t filledQuantity(std::int64_t clOrdId) const;

    /**
     * Sets the first order id, before any order is submitted. ClOrdIDs must
     * not repeat across restarts of a session, so a live deployment passes
     * the persisted nextOrderId() of the previous run, or a value derived
     * from the trading day. The default of 1 suits tests and simulation only.
     *
     * @throws std::logic_error if an order was already submitted
     * @throws std::invalid_argument if first is outside the id range
     */
    void startOrderIdsAt(std::int64_t first);

    /** The id the next order will take; persist it to restart without reuse. */
    std::int64_t nextOrderId() const;

    /** Whether this gateway could have issued the ClOrdID, finished orders included. */
    bool issued(std::int64_t clOrdId) const;

    /** Orders in the table. */
    int openOrderCount() const { return live_.load(std::memory_order_acquire); }

    std::int64_t submittedCount() const { return submitted_.load(std::memory_order_acquire); }

    /** New orders handed to the venue so far. */
    std::int64_t deliveredCount() const { return delivered_.load(std::memory_order_acquire); }

    std::int64_t ringFullCount() const { return ringFull_.load(std::memory_order_acquire); }

    /** Queued orders stopped by a kill before reaching the venue. */
    std::int64_t blockedByKillCount() const { return blocked_.load(std::memory_order_acquire); }

    std::int64_t overfillCount() const { return overfills_.load(std::memory_order_acquire); }

    /** Fills for no matching working order. */
    std::int64_t unsolicitedFillCount() const {
        return unsolicited_.load(std::memory_order_acquire);
    }

    /** Non-fill reports that matched nothing: usually a benign race, always counted. */
    std::int64_t unmatchedReportCount() const {
        return unmatched_.load(std::memory_order_acquire);
    }

    /** Listener exceptions caught on the venue thread; each latches KILL_GATEWAY. */
    std::int64_t listenerFailureCount() const {
        return listenerFailures_.load(std::memory_order_acquire);
    }

private:
    enum class State : std::uint8_t { QUEUED, SENT, LIVE };

    struct Slot {
        std::int64_t orderId = 0;   // 0 = free
        std::int64_t quantity = 0;  // FIX total, including what has filled
        std::int64_t filled = 0;
        std::int64_t reserved = 0;  // held at the gate for this order
        std::int64_t pendingQuantity = 0;
        double price = 0;
        double pendingPrice = 0;
        std::int64_t version = 0;
        std::int64_t ackedVersion = 0;   // the name the venue knows the order by
        int symbolId = 0;
        int prev = -1;              // per-symbol list of working orders
        int next = -1;
        Side side = Side::BUY;
        State state = State::QUEUED;
        bool replacePending = false;
        bool cancelPending = false;
        bool reduceOnly = false;
    };

    class SpinGuard;

    Slot* find(std::int64_t clOrdId);
    const Slot* find(std::int64_t clOrdId) const;
    bool crossesOwnOrder(int symbolId, Side side, std::int64_t quantity, double price,
                         const Slot* self);
    void link(int index);
    void widenBound(int symbolId, Side side, double price);
    void finish(Slot& slot);
    void resolveReplace(Slot& slot, bool accepted);
    int sweep(std::int64_t timestampNanos, bool skipReduceOnly);
    void onDrained(std::uint8_t type, std::int64_t clOrdId, std::int64_t refId, int symbolId,
                   Side side, std::int64_t quantity, double price, std::int64_t timestampNanos);
    void venueLoop();

    using OrderVec = std::vector<const OrderListener*>;
    using VenueVec = std::vector<const VenueListener*>;

    OrderRingBuffer ring_;
    HftRiskGate& riskGate_;
    const bool busySpin_;
    std::atomic<const OrderVec*> listeners_{nullptr};
    std::atomic<const VenueVec*> venueListeners_{nullptr};
    std::deque<OrderListener> orderListenerStore_;   // deque keeps addresses stable
    std::deque<VenueListener> venueListenerStore_;
    std::vector<std::unique_ptr<const OrderVec>> orderLists_;
    std::vector<std::unique_ptr<const VenueVec>> venueLists_;
    SelfTradeHook selfTradeHook_ = nullptr;
    void* selfTradeContext_ = nullptr;

    std::vector<Slot> slots_;
    std::vector<int> symbolHeads_;
    std::vector<double> highestBuy_;    // bound on our working buys, by symbol
    std::vector<double> lowestSell_;    // bound on our working sells, by symbol
    std::vector<int> workingBuys_;
    std::vector<int> workingSells_;
    const std::int64_t slotMask_;

    mutable std::atomic_flag lock_ = ATOMIC_FLAG_INIT;
    std::mutex setupMutex_;
    std::atomic<bool> running_{false};
    std::atomic<bool> sweepPending_{false};
    std::atomic<std::int64_t> deadManTimeout_{0};
    std::atomic<std::int64_t> lastHeartbeat_{0};
    std::uint64_t sweptEpoch_ = 0;   // the polling thread only
    std::int64_t nextId_ = 1;        // under lock_
    std::int64_t firstId_ = 1;       // under lock_

    std::atomic<int> live_{0};
    std::atomic<std::int64_t> delivered_{0};
    std::atomic<std::int64_t> submitted_{0};
    std::atomic<std::int64_t> ringFull_{0};
    std::atomic<std::int64_t> blocked_{0};
    std::atomic<std::int64_t> overfills_{0};
    std::atomic<std::int64_t> unsolicited_{0};
    std::atomic<std::int64_t> unmatched_{0};
    std::atomic<std::int64_t> listenerFailures_{0};
    std::thread venueThread_;
};

}  // namespace quantfinlib
