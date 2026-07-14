// Port of com.quantfinlib.backtest.strategies.RsiStrategy: RSI mean
// reversion — buy when RSI crosses up through the oversold level, sell
// when it crosses down through the overbought level. Uses the library
// RSI (Wilder smoothing), exactly like the Java strategy.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "quantfinlib/backtest/signal.hpp"
#include "quantfinlib/backtest/strategies/strategy_format.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/indicators/indicators.hpp"

namespace quantfinlib {

class RsiStrategy final : public TradingStrategy {
public:
    RsiStrategy(int period, double oversold, double overbought)
            : period_(period), oversold_(oversold), overbought_(overbought) {}

    std::string name() const override {
        return "RSI(" + std::to_string(period_) + "," + strategies_detail::javaDouble(oversold_)
               + "," + strategies_detail::javaDouble(overbought_) + ")";
    }

    void init(const BarSeries& series) override {
        rsi_ = Indicators::rsi(series.closes(), period_);
    }

    Signal onBar(int i) override {
        std::size_t k = static_cast<std::size_t>(i);
        if (i < 1 || std::isnan(rsi_[k]) || std::isnan(rsi_[k - 1])) {
            return Signal::HOLD;
        }
        if (rsi_[k - 1] <= oversold_ && rsi_[k] > oversold_) {
            return Signal::BUY;
        }
        if (rsi_[k - 1] >= overbought_ && rsi_[k] < overbought_) {
            return Signal::SELL;
        }
        return Signal::HOLD;
    }

private:
    int period_;
    double oversold_;
    double overbought_;
    std::vector<double> rsi_;
};

}  // namespace quantfinlib
