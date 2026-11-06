// Port of com.quantfinlib.execution.PortfolioExecutor: true multi-symbol
// portfolio-level scheduling -- a basket (rebalance, transition, program
// trade) executed as one coordinated schedule rather than N independent
// parents. Each symbol keeps its own BenchmarkExecutor child, and the
// portfolio layer applies two overlays that only exist at basket level:
//
//  1. Leg balance -- the buy leg and the sell leg must stay in step, or
//     the basket carries unintended net market exposure mid-flight. When
//     the projected net filled notional would breach maxNetNotional, the
//     interval throttles the leg that is ahead. It never accelerates the
//     lagging leg.
//  2. Capacity allocation -- maxIntervalNotional caps the basket's total
//     demand per interval. When it binds, capacity goes to the symbols
//     carrying the most residual risk: weight ~ (1 + volatility regime) *
//     due notional by default, or (with useRiskModel) weight ~ (1 +
//     marginal contribution to BASKET variance) * due notional.
//
// Both overlays only ever REDUCE a child's own due quantity, so per-symbol
// benchmark integrity holds by construction. Usage: add() each child once,
// then each interval call decide() with per-symbol MarketState snapshots
// and route the returned dues; report fills via onFill (which also
// maintains the net ledger). Cross-asset, zero allocation per decide,
// single writer. IllegalArgumentException maps to std::invalid_argument;
// IllegalStateException maps to std::logic_error.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/benchmark_executor.hpp"
#include "quantfinlib/microstructure/ewma_covariance.hpp"

namespace quantfinlib {

class PortfolioExecutor {
public:
    /**
     * @param maxNetNotional      leg-balance band: |filled buys - filled
     *                            sells| (projected through this interval)
     *                            stays within this; +inf disables
     * @param maxIntervalNotional total basket demand per interval; +inf disables
     */
    struct Config {
        double maxNetNotional;
        double maxIntervalNotional;

        Config(double maxNetNotional_, double maxIntervalNotional_);

        /** No portfolio constraints: children pass through untouched. */
        static Config unconstrained();
    };

    PortfolioExecutor(int maxSymbols, const Config& config);

    /**
     * Upgrades the capacity allocation from the diagonal approximation to
     * true basket risk: with a covariance model, a binding
     * maxIntervalNotional flows to the symbols whose REMAINING position
     * contributes most to portfolio variance -- two correlated buys carry
     * more joint timing risk than their individual vols admit, and a
     * natural hedge carries less. Handle i maps to covariance symbol i;
     * feed the model one return vector per interval on your own clock.
     */
    void useRiskModel(EwmaCovariance& model);

    /** Registers a child parent order; returns its handle for decide/onFill. */
    int add(BenchmarkExecutor child);

    /**
     * One portfolio interval: asks every child for its own due quantity,
     * then applies the leg-balance band and the capacity allocation.
     * dueOut[handle] receives the shares to send per symbol.
     *
     * @param scheduleFraction elapsed fraction of the execution horizon
     * @param states           per-handle market snapshots (index = handle)
     * @param dueOut           per-handle output, length >= size()
     */
    void decide(double scheduleFraction, const std::vector<BenchmarkExecutor::MarketState>& states,
               std::vector<std::int64_t>& dueOut);

    /**
     * A fill for one child: forwards to its executor and maintains the net
     * ledger. Report every fill through THIS method, never through
     * child(h).onFill(...) -- the child call advances that symbol's
     * schedule but silently bypasses the buy/sell notional ledger the
     * leg-balance band reads.
     */
    void onFill(int handle, std::int64_t qty, double price);

    /** A child order went out; the leg band and the child both count it from now on. */
    void onSent(int handle, std::int64_t qty);

    /** Working quantity came back unfilled. */
    void onCancelled(int handle, std::int64_t qty);

    /** Signed net filled notional: buys - sells. The leg-balance ledger. */
    double netNotional() const { return buyFilledNotional_ - sellFilledNotional_; }

    bool done() const;
    int size() const { return count_; }

    /**
     * The child executor behind a handle -- for progress/drift reads and
     * for feeding onMarketVolume to VWAP/POV children. Do NOT report fills
     * via child(h).onFill(...): fills must go through onFill(int, long,
     * double) so the leg-balance ledger sees them.
     */
    BenchmarkExecutor& child(int handle) { return children_[static_cast<std::size_t>(handle)]; }
    const BenchmarkExecutor& child(int handle) const { return children_[static_cast<std::size_t>(handle)]; }

private:
    void fillRiskFactors(const std::vector<BenchmarkExecutor::MarketState>& states);
    void applyLegBand(std::vector<std::int64_t>& dueOut);
    void scaleSide(Side side, double scale, std::vector<std::int64_t>& dueOut);

    Config config_;
    int maxSymbols_;
    std::vector<BenchmarkExecutor> children_;  // capacity reserved to maxSymbols_ so handles stay stable
    std::vector<double> lastMid_;        // last finite price seen per symbol
    std::vector<double> dueNotional_;    // scratch, valid within decide()
    std::vector<double> signedRemaining_;// scratch: signed remaining notional
    std::vector<double> riskFactor_;     // scratch: capacity weight multiplier
    int count_ = 0;

    EwmaCovariance* riskModel_ = nullptr;  // optional: upgrades the risk weights

    double buyFilledNotional_ = 0;
    double sellFilledNotional_ = 0;
};

}  // namespace quantfinlib
