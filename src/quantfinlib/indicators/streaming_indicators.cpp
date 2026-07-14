#include "quantfinlib/indicators/streaming_indicators.hpp"

#include <algorithm>
#include <cmath>

namespace quantfinlib {

namespace {

double toRsi(double avgGain, double avgLoss) {
    if (avgLoss == 0) {
        return avgGain == 0 ? 50 : 100;
    }
    return 100 - 100 / (1 + avgGain / avgLoss);
}

}  // namespace

double StreamingIndicators::Rsi::update(double v) {
    if (std::isnan(prev_)) {
        prev_ = v;
        return std::numeric_limits<double>::quiet_NaN();
    }
    double d = v - prev_;
    prev_ = v;
    changes_++;
    if (changes_ < period_) {
        gainSum_ += std::max(d, 0.0);
        lossSum_ += std::max(-d, 0.0);
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (changes_ == period_) {
        avgGain_ = (gainSum_ + std::max(d, 0.0)) / period_;
        avgLoss_ = (lossSum_ + std::max(-d, 0.0)) / period_;
    } else {
        avgGain_ = (avgGain_ * (period_ - 1) + std::max(d, 0.0)) / period_;
        avgLoss_ = (avgLoss_ * (period_ - 1) + std::max(-d, 0.0)) / period_;
    }
    value_ = toRsi(avgGain_, avgLoss_);
    return value_;
}

double StreamingIndicators::Macd::update(double v) {
    double f = fast_.update(v);
    double s = slow_.update(v);
    if (std::isnan(s)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    line_ = f - s;
    signal_ = signalEma_.update(line_);
    histogram_ = std::isnan(signal_) ? std::numeric_limits<double>::quiet_NaN()
                                     : line_ - signal_;
    return line_;
}

}  // namespace quantfinlib
