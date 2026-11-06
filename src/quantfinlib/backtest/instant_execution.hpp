// Port of com.quantfinlib.backtest.InstantExecution: baseline execution
// model — the full quantity fills at the bar's open (the first price after
// a prior-close signal) or, with FillTiming::SAME_BAR_CLOSE, at the bar's
// close, with commission and slippage folded into the all-in price.
#pragma once

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/execution_model.hpp"

namespace quantfinlib {

class InstantExecution final : public ExecutionModel {
public:
    InstantExecution(double commissionRate, double slippageRate,
                     FillTiming timing = FillTiming::NEXT_BAR_OPEN)
            : costRate_(commissionRate + slippageRate), timing_(timing) {}

    static InstantExecution from(const BacktestConfig& config) {
        return InstantExecution(config.commissionRate, config.slippageRate, config.fillTiming);
    }

    double referencePrice(const BarSeries& series, int index) const override {
        return timing_ == FillTiming::SAME_BAR_CLOSE ? series.close(index) : series.open(index);
    }

    double worstCaseCostFraction() const override {
        // The all-in fill is exactly reference * (1 + costRate); declare it
        // so entry sizing can spend cash to the last share without
        // overdrawing.
        return costRate_;
    }

    std::vector<Execution> execute(Side side, std::int64_t requestedQty, const BarSeries& series,
                                   int index) override {
        if (requestedQty <= 0) {
            return {};
        }
        double allIn = referencePrice(series, index) * (1 + sign(side) * costRate_);
        return {Execution{series.symbol(), side, allIn, requestedQty, series.timestamp(index),
                          "PRIMARY"}};
    }

private:
    double costRate_;
    FillTiming timing_;
};

}  // namespace quantfinlib
