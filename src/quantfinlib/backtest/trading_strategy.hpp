// Port of com.quantfinlib.backtest.TradingStrategy (Java interface): a
// bar-driven trading strategy. init() is called once by the backtester
// to precompute indicators; onBar() is then invoked for each bar in
// order and must be allocation-free for low-latency execution. The Java
// default methods become virtuals with the same defaults.
#pragma once

#include <string>

#include "quantfinlib/backtest/signal.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {

class TradingStrategy {
public:
    virtual ~TradingStrategy() = default;

    virtual std::string name() const = 0;

    virtual void init(const BarSeries& series) = 0;

    virtual Signal onBar(int index) = 0;

    /** Optional per-trade stop loss as a fraction (0 = disabled). */
    virtual double stopLossPct() const { return 0; }

    /** Optional per-trade take profit as a fraction (0 = disabled). */
    virtual double takeProfitPct() const { return 0; }
};

}  // namespace quantfinlib
