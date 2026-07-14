// Port of com.quantfinlib.backtest.InstantExecution: baseline execution
// model — the full quantity fills at the bar close with commission and
// slippage folded into the all-in price, equivalent to the classic
// Backtester fill assumption.
#pragma once

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/execution_model.hpp"

namespace quantfinlib {

class InstantExecution final : public ExecutionModel {
public:
    InstantExecution(double commissionRate, double slippageRate)
            : costRate_(commissionRate + slippageRate) {}

    static InstantExecution from(const BacktestConfig& config) {
        return InstantExecution(config.commissionRate, config.slippageRate);
    }

    double worstCaseCostFraction() const override {
        // The all-in fill is exactly close * (1 + costRate); declare it
        // so entry sizing can spend cash to the last share without
        // overdrawing.
        return costRate_;
    }

    std::vector<Execution> execute(Side side, std::int64_t requestedQty, const BarSeries& series,
                                   int index) override {
        if (requestedQty <= 0) {
            return {};
        }
        double allIn = series.close(index) * (1 + sign(side) * costRate_);
        return {Execution{series.symbol(), side, allIn, requestedQty, series.timestamp(index),
                          "PRIMARY"}};
    }

private:
    double costRate_;
};

}  // namespace quantfinlib
