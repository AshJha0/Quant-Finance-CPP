// Port of com.quantfinlib.backtest.strategies.MacdStrategy: MACD
// signal-line crossover — buy when MACD crosses above its signal line,
// sell on the reverse.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "quantfinlib/backtest/signal.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/indicators/indicators.hpp"

namespace quantfinlib {

class MacdStrategy final : public TradingStrategy {
public:
    MacdStrategy(int fast, int slow, int signalPeriod)
            : fast_(fast), slow_(slow), signalPeriod_(signalPeriod) {}

    MacdStrategy() : MacdStrategy(12, 26, 9) {}

    std::string name() const override {
        return "MACD(" + std::to_string(fast_) + "," + std::to_string(slow_) + ","
               + std::to_string(signalPeriod_) + ")";
    }

    void init(const BarSeries& series) override {
        Indicators::Macd m = Indicators::macd(series.closes(), fast_, slow_, signalPeriod_);
        line_ = std::move(m.line);
        signal_ = std::move(m.signal);
    }

    Signal onBar(int i) override {
        std::size_t k = static_cast<std::size_t>(i);
        if (i < 1 || std::isnan(signal_[k]) || std::isnan(signal_[k - 1])) {
            return Signal::HOLD;
        }
        if (line_[k - 1] <= signal_[k - 1] && line_[k] > signal_[k]) {
            return Signal::BUY;
        }
        if (line_[k - 1] >= signal_[k - 1] && line_[k] < signal_[k]) {
            return Signal::SELL;
        }
        return Signal::HOLD;
    }

private:
    int fast_;
    int slow_;
    int signalPeriod_;
    std::vector<double> line_;
    std::vector<double> signal_;
};

}  // namespace quantfinlib
