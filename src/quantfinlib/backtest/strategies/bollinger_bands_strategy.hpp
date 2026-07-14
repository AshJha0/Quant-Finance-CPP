// Port of com.quantfinlib.backtest.strategies.BollingerBandsStrategy:
// Bollinger Band mean reversion — buy when the close dips below the
// lower band, sell when it recovers to the middle band or stretches
// above the upper band.
#pragma once

#include <cmath>
#include <string>
#include <vector>

#include "quantfinlib/backtest/signal.hpp"
#include "quantfinlib/backtest/strategies/strategy_format.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/indicators/indicators.hpp"

namespace quantfinlib {

class BollingerBandsStrategy final : public TradingStrategy {
public:
    BollingerBandsStrategy(int period, double k) : period_(period), k_(k) {}

    BollingerBandsStrategy() : BollingerBandsStrategy(20, 2.0) {}

    std::string name() const override {
        return "BOLLINGER(" + std::to_string(period_) + "," + strategies_detail::javaDouble(k_)
               + ")";
    }

    void init(const BarSeries& series) override {
        close_ = series.closes();
        Indicators::Bollinger b = Indicators::bollinger(close_, period_, k_);
        upper_ = std::move(b.upper);
        middle_ = std::move(b.middle);
        lower_ = std::move(b.lower);
    }

    Signal onBar(int i) override {
        std::size_t k = static_cast<std::size_t>(i);
        if (std::isnan(middle_[k])) {
            return Signal::HOLD;
        }
        if (close_[k] < lower_[k]) {
            return Signal::BUY;
        }
        if ((close_[k] >= middle_[k] && close_[k] > upper_[k])
            || (close_[k] >= middle_[k] && i > 0 && close_[k - 1] < middle_[k - 1])) {
            return Signal::SELL;
        }
        return Signal::HOLD;
    }

private:
    int period_;
    double k_;
    std::vector<double> upper_;
    std::vector<double> middle_;
    std::vector<double> lower_;
    std::vector<double> close_;
};

}  // namespace quantfinlib
