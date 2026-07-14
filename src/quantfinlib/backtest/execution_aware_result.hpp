// Port of com.quantfinlib.backtest.ExecutionAwareResult: result of an
// execution-aware backtest — the standard BacktestResult (equity curve,
// trades, performance metrics) plus the full parent-order / child-fill
// history, with one-call TCA per parent order. The result keeps a COPY
// of the bar series (Java holds a reference to the immutable series)
// so tca() stays valid independent of the caller's series lifetime.
#pragma once

#include <vector>

#include "quantfinlib/backtest/backtest_result.hpp"
#include "quantfinlib/backtest/parent_order.hpp"
#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/microstructure/transaction_cost_analyzer.hpp"

namespace quantfinlib {

class ExecutionAwareResult {
public:
    ExecutionAwareResult(BacktestResult backtest, std::vector<ParentOrder> parentOrders,
                         BarSeries series);

    const BacktestResult& backtest() const { return backtest_; }

    const std::vector<ParentOrder>& parentOrders() const { return parentOrders_; }

    /** Every child fill of the run, in execution order. */
    std::vector<Execution> allFills() const;

    /**
     * Transaction cost analysis for one parent order: arrival mid =
     * close at the signal bar, market VWAP = volume-weighted close over
     * the fill interval, per-fill mid = close of the fill bar.
     *
     * @throws std::invalid_argument if the parent has no fills
     */
    transaction_cost_analyzer::TcaReport tca(const ParentOrder& parent) const;

private:
    BacktestResult backtest_;
    std::vector<ParentOrder> parentOrders_;
    BarSeries series_;
};

}  // namespace quantfinlib
