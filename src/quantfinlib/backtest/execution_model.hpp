// Port of com.quantfinlib.backtest.ExecutionModel (Java interface): how
// parent orders turn into fills in an execution-aware backtest
// (ExecutionAwareBacktester). The engine calls execute() once per bar
// while a parent order is working; anything not filled carries over to
// the next bar.
//
// Returned fill prices are ALL-IN (fees and spread folded in), so the
// engine's cash accounting is simply price * quantity.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/microstructure/execution.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class ExecutionModel {
public:
    virtual ~ExecutionModel() = default;

    /**
     * Notification that a new parent order has been created (entry or
     * exit). Stateful models (e.g. IcebergExecution) reset per-parent
     * state here.
     */
    virtual void onParentOrder(Side /*side*/, std::int64_t /*totalQuantity*/,
                               int /*signalIndex*/) {}

    /**
     * Executes up to requestedQty on this bar. Must never fill more than
     * requested; may fill less (or nothing) — the remainder is retried
     * on subsequent bars.
     */
    virtual std::vector<Execution> execute(Side side, std::int64_t requestedQty,
                                           const BarSeries& series, int index) = 0;

    /**
     * The price this model's fills are anchored to on the given bar —
     * the engine budgets entry requests as
     * cash / (referencePrice * (1 + worstCaseCostFraction())).
     * Default: the bar close. A model that fills off a different price
     * point (e.g. LastLookExecution fills at the OPEN) must override
     * this, or a gap between close and its actual anchor lets a
     * fully-filled request overdraw cash.
     */
    virtual double referencePrice(const BarSeries& series, int index) const {
        return series.close(index);
    }

    /**
     * Upper bound on this model's all-in cost as a fraction of
     * referencePrice (spread + fees + slippage). The engine uses it to
     * size entries so that a fully-filled parent can never overdraw
     * cash — a model whose fills can cost more than
     * referencePrice * (1 + worstCaseCostFraction()) MUST override one
     * or both methods, or the backtest silently trades on margin it
     * doesn't have. Wrappers must DELEGATE both to the model that
     * actually prices the fills. Default 1%.
     */
    virtual double worstCaseCostFraction() const { return 0.01; }
};

}  // namespace quantfinlib
