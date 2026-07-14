// Port of com.quantfinlib.backtest.strategies.SmaCrossStrategy:
// golden/death cross — buy when the fast SMA crosses above the slow
// SMA, sell on the reverse cross. Gate maps to std::invalid_argument.
#pragma once

#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantfinlib/backtest/signal.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/indicators/indicators.hpp"

namespace quantfinlib {

class SmaCrossStrategy final : public TradingStrategy {
public:
    SmaCrossStrategy(int fastPeriod, int slowPeriod)
            : fastPeriod_(fastPeriod), slowPeriod_(slowPeriod) {
        if (fastPeriod >= slowPeriod) {
            throw std::invalid_argument("fastPeriod must be < slowPeriod");
        }
    }

    std::string name() const override {
        return "SMA_CROSS(" + std::to_string(fastPeriod_) + "," + std::to_string(slowPeriod_)
               + ")";
    }

    void init(const BarSeries& series) override {
        fast_ = Indicators::sma(series.closes(), fastPeriod_);
        slow_ = Indicators::sma(series.closes(), slowPeriod_);
    }

    Signal onBar(int i) override {
        std::size_t k = static_cast<std::size_t>(i);
        if (i < 1 || std::isnan(slow_[k]) || std::isnan(slow_[k - 1])) {
            return Signal::HOLD;
        }
        if (fast_[k - 1] <= slow_[k - 1] && fast_[k] > slow_[k]) {
            return Signal::BUY;
        }
        if (fast_[k - 1] >= slow_[k - 1] && fast_[k] < slow_[k]) {
            return Signal::SELL;
        }
        return Signal::HOLD;
    }

private:
    int fastPeriod_;
    int slowPeriod_;
    std::vector<double> fast_;
    std::vector<double> slow_;
};

}  // namespace quantfinlib
