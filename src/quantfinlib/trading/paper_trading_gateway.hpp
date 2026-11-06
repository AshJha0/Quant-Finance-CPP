// Port of com.quantfinlib.trading.PaperTradingGateway (Java): quote-driven
// paper trading venue -- closes the research-to-production loop by running
// real strategy + risk-gate code against simulated fills.
//
//  - Feed top-of-book quotes with onQuote; market orders fill at the touch,
//    resting limit orders fill when the market crosses them.
//  - Full account tracking: signed positions with average cost, cash,
//    realized/unrealized P&L, commission, and mark-to-market equity.
//
// Thread-safety: Java synchronizes every method on the gateway itself; the
// C++ port uses one std::mutex guarding all mutable state, so snapshot()
// returns an internally consistent view of the account under a single lock
// acquisition -- safe to read from a dashboard thread while another thread
// trades.
//
// Order validation always runs: quantity must be positive and prices finite
// and positive, otherwise the order is REJECTED and logged. Attach an
// HftRiskGate with attachRiskGate to put the live gate in front of the paper
// venue: reservations, limits, collar and kill behave as on the fast lane,
// quotes refresh its reference, and a symbol with no id mapping is rejected.
// With no gate attached, validation is the only pre-trade check.
#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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

    explicit PaperTradingGateway(double initialCash) : PaperTradingGateway(initialCash, 0.0) {}

    PaperTradingGateway(double initialCash, double commissionRate)
            : commissionRate_(commissionRate), cash_(initialCash) {}

    PaperTradingGateway(const PaperTradingGateway&) = delete;
    PaperTradingGateway& operator=(const PaperTradingGateway&) = delete;

    /**
     * Puts a gate in front of every order. Call before the first order.
     * symbolIds maps this gateway's symbol names to the gate's dense ids. The
     * gate must outlive this gateway.
     */
    void attachRiskGate(HftRiskGate& gate, std::unordered_map<std::string, int> symbolIds);

    // ------------------------------------------------------------------
    // Market data in
    // ------------------------------------------------------------------

    /**
     * Updates the top of book and fills any resting limit orders that now
     * cross. A quote with an unusable or crossed price is logged and ignored.
     */
    void onQuote(const std::string& symbol, double bid, double ask);

    // ------------------------------------------------------------------
    // Order entry
    // ------------------------------------------------------------------

    std::int64_t submitLimit(const std::string& symbol, Side side, std::int64_t quantity,
                             double price);

    std::int64_t submitMarket(const std::string& symbol, Side side, std::int64_t quantity);

    bool cancel(std::int64_t orderId);

    /** @throws std::invalid_argument if orderId is unknown */
    OrderStatus status(std::int64_t orderId) const;

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

    static constexpr int REJECTION_LOG_CAP = 1000;

private:
    struct WorkingOrder {
        std::int64_t id;
        std::string symbol;
        Side side;
        double limitPrice;   // NaN = market
        std::int64_t quantity;
        OrderStatus status;
    };

    struct Position {
        double quantity = 0;
        double avgCost = 0;
    };

    struct Quote {
        double bid;
        double ask;
        double mid() const { return (bid + ask) / 2; }
    };

    double equityLocked() const;
    std::unordered_map<std::string, double> positionsSnapshotLocked() const;
    bool passesRiskGate(WorkingOrder& order, double referencePrice);
    void logRejection(const std::string& entry);
    void fill(WorkingOrder& order, double price);
    void applyFill(const std::string& symbol, Side side, double price, std::int64_t qty);

    mutable std::mutex mutex_;
    const double commissionRate_;
    std::unordered_map<std::string, Quote> quotes_;
    std::unordered_map<std::int64_t, WorkingOrder> orders_;
    std::vector<std::int64_t> restingIds_;
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
