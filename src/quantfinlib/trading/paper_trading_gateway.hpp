// Port of com.quantfinlib.trading.PaperTradingGateway (Java): quote-driven
// paper trading venue -- closes the research-to-production loop by running
// real strategy + risk-gate code against simulated fills.
//
//  - Feed sized top-of-book quotes with onQuote and prints with onTrade. Every
//    order fills through a QueueFillSimulator per symbol: an aggressive order
//    takes only displayed depth, a market remainder is cancelled, a resting
//    limit waits its turn in the queue, and order and market-data latency
//    apply. See queue_fill_simulator.hpp.
//  - Full account tracking: signed positions with average cost, cash,
//    realized/unrealized P&L, commission, and mark-to-market equity.
//
// Thread-safety: Java synchronizes every method on the gateway itself; the
// C++ port uses one std::mutex guarding all mutable state, so snapshot()
// returns an internally consistent view of the account under a single lock
// acquisition -- safe to read from a dashboard thread while another thread
// trades.
//
// Time is event time: onQuote and onTrade carry a timestamp and the gateway's
// clock is the latest one, so a run replays exactly. Orders are stamped with
// that clock and reach the venue a latency later, when a feed event moves the
// clock past their arrival. Status, position and cash change when the report
// comes back. Events must arrive in time order across symbols.
//
// Order validation always runs: quantity must be positive and prices finite
// and positive, otherwise the order is REJECTED and logged. The risk gate is
// a constructor argument: reservations, limits, collar and kill behave as on
// the fast lane, quotes refresh its reference, and a symbol with no id mapping
// is rejected. Running with no gate takes the factory named
// unsafeWithoutRiskGate, for research where a limit breach cannot matter.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/execution/queue_fill_simulator.hpp"
#include "quantfinlib/orderbook/side.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

namespace quantfinlib {

/** Lifecycle state of a gateway order. */
enum class OrderStatus { NEW, PARTIALLY_FILLED, FILLED, CANCELED, REJECTED };

/** Fill callback: orderId, symbol, side, fill price, quantity, timestampNanos. */
using ExecutionListener = std::function<void(std::int64_t orderId, const std::string& symbol,
                                              Side side, double price, std::int64_t quantity,
                                              std::int64_t timestampNanos)>;

class PaperTradingGateway {
public:
    /** One internally consistent view of the whole account (single lock acquisition). */
    struct AccountSnapshot {
        double cash;
        double equity;
        double realizedPnl;
        int rejectionCount;
        std::unordered_map<std::string, double> positions;
    };

    using FillConfig = QueueFillSimulator::Config;

    /**
     * @param gate      must outlive this gateway
     * @param symbolIds this gateway's symbol names to the gate's dense ids
     * @param fills     latency and seed; each symbol's generator mixes the
     *                  seed with its name, so symbols do not share a stream
     */
    PaperTradingGateway(double initialCash, double commissionRate, HftRiskGate& gate,
                        std::unordered_map<std::string, int> symbolIds,
                        const FillConfig& fills = FillConfig());

    /** No pre-trade limits at all: validation is the only check. Never for a live-like run. */
    static PaperTradingGateway unsafeWithoutRiskGate(double initialCash, double commissionRate = 0,
                                                     const FillConfig& fills = FillConfig()) {
        return PaperTradingGateway(initialCash, commissionRate, fills);
    }

    PaperTradingGateway(const PaperTradingGateway&) = delete;
    PaperTradingGateway& operator=(const PaperTradingGateway&) = delete;

    // ------------------------------------------------------------------
    // Market data in
    // ------------------------------------------------------------------

    /**
     * Updates the top of book and works the orders it affects. A quote with
     * an unusable or crossed price, a negative size or a time before the
     * clock is logged and ignored.
     */
    void onQuote(const std::string& symbol, std::int64_t nanos, double bid, std::int64_t bidSize,
                 double ask, std::int64_t askSize);

    /** A print: moves the queues. Report it before the quote that reflects it. */
    void onTrade(const std::string& symbol, std::int64_t nanos, double price, std::int64_t quantity);

    // ------------------------------------------------------------------
    // Order entry
    // ------------------------------------------------------------------

    std::int64_t submitLimit(const std::string& symbol, Side side, std::int64_t quantity,
                             double price);

    std::int64_t submitMarket(const std::string& symbol, Side side, std::int64_t quantity);

    /**
     * Sends a cancel; false when the order is finished or a cancel is already
     * on its way. With latency the order can still fill before the cancel lands.
     */
    bool cancel(std::int64_t orderId);

    /** @throws std::invalid_argument if orderId is unknown */
    OrderStatus status(std::int64_t orderId) const;

    /** Shares filled so far. @throws std::invalid_argument if orderId is unknown */
    std::int64_t filledQuantity(std::int64_t orderId) const;

    void addExecutionListener(ExecutionListener listener);

    // ------------------------------------------------------------------
    // Account
    // ------------------------------------------------------------------

    double position(const std::string& symbol) const;

    double cash() const;

    double realizedPnl() const;

    /** Mark-to-market equity at current mids. */
    double equity() const;

    /** The first REJECTION_LOG_CAP rejection messages; the snapshot's
     *  rejectionCount keeps counting past the cap. */
    std::vector<std::string> rejectionLog() const;

    /** Snapshot of non-zero positions by symbol (for dashboards/monitoring). */
    std::unordered_map<std::string, double> positionsSnapshot() const;

    AccountSnapshot snapshot() const;

    /** Mark-outs of this symbol's fills; all zero if it never traded. */
    QueueFillSimulator::AdverseSelection adverseSelection(const std::string& symbol) const;

    static constexpr int REJECTION_LOG_CAP = 1000;

private:
    struct WorkingOrder {
        std::int64_t id;
        std::string symbol;
        Side side;
        std::int64_t quantity;
        std::int64_t filled;
        OrderStatus status;
        std::int64_t simId;
        bool cancelSent;
    };

    struct Position {
        double quantity = 0;
        double avgCost = 0;
    };

    struct Venue {
        explicit Venue(const FillConfig& config) : simulator(config) {}
        QueueFillSimulator simulator;
        std::unordered_map<std::int64_t, std::int64_t> orderIds;  // simulator id to ours
    };

    PaperTradingGateway(double initialCash, double commissionRate, const FillConfig& fills)
            : commissionRate_(commissionRate), fillConfig_(fills), cash_(initialCash) {}

    double equityLocked() const;
    std::unordered_map<std::string, double> positionsSnapshotLocked() const;
    bool passesRiskGate(WorkingOrder& order, double referencePrice);
    void logRejection(const std::string& entry);
    Venue& venue(const std::string& symbol);
    WorkingOrder& newOrder(const std::string& symbol, Side side, std::int64_t quantity);
    void settle();
    void applyReport(Venue& venue, const QueueFillSimulator::Report& report);
    void applyFill(const std::string& symbol, Side side, double price, std::int64_t qty);

    mutable std::mutex mutex_;
    const double commissionRate_;
    const FillConfig fillConfig_;
    std::map<std::string, Venue> venues_;  // ordered, so settling is deterministic
    std::int64_t clock_ = 0;
    std::unordered_map<std::int64_t, WorkingOrder> orders_;
    HftRiskGate* riskGate_ = nullptr;
    std::unordered_map<std::string, int> gateSymbolIds_;
    std::unordered_map<std::string, Position> positions_;
    std::vector<ExecutionListener> listeners_;
    std::vector<std::string> rejectionLog_;
    int rejectionCount_ = 0;
    double cash_;
    double realizedPnl_ = 0;
    std::int64_t nextId_ = 1;
};

}  // namespace quantfinlib
