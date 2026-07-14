// Port of com.quantfinlib.indicators.StreamingIndicators (Java): incremental
// O(1)-per-tick indicators for live strategies. Numerically identical to the
// batch Indicators implementations (same seeding and smoothing), so a
// strategy backtested on batch arrays behaves the same when run live on the
// streaming versions. Instances are single-threaded by design.
#pragma once

#include <cstdint>
#include <limits>
#include <vector>

namespace quantfinlib {

class StreamingIndicators {
public:
    StreamingIndicators() = delete;

    /** Simple moving average over a fixed window; NaN until the window fills. */
    class Sma {
    public:
        explicit Sma(int period)
            : window_(period, 0.0), period_(period) {}

        double update(double v) {
            if (count_ >= period_) {
                sum_ -= window_[idx_];
            }
            window_[idx_] = v;
            sum_ += v;
            idx_ = idx_ + 1 == period_ ? 0 : idx_ + 1;
            count_++;
            value_ = count_ >= period_ ? sum_ / period_
                                       : std::numeric_limits<double>::quiet_NaN();
            return value_;
        }

        double value() const { return value_; }

    private:
        std::vector<double> window_;
        int period_;
        double sum_ = 0;
        std::int64_t count_ = 0;
        int idx_ = 0;
        double value_ = std::numeric_limits<double>::quiet_NaN();
    };

    /** Exponential moving average seeded with the SMA of the first period values. */
    class Ema {
    public:
        explicit Ema(int period) : period_(period), k_(2.0 / (period + 1)) {}

        double update(double v) {
            count_++;
            if (count_ < period_) {
                seedSum_ += v;
                return std::numeric_limits<double>::quiet_NaN();
            }
            if (count_ == period_) {
                value_ = (seedSum_ + v) / period_;
            } else {
                value_ += (v - value_) * k_;
            }
            return value_;
        }

        double value() const { return value_; }

    private:
        int period_;
        double k_;
        double seedSum_ = 0;
        std::int64_t count_ = 0;
        double value_ = std::numeric_limits<double>::quiet_NaN();
    };

    /** Wilder RSI; NaN until period price changes have been observed. */
    class Rsi {
    public:
        explicit Rsi(int period) : period_(period) {}

        double update(double v);

        double value() const { return value_; }

    private:
        int period_;
        double prev_ = std::numeric_limits<double>::quiet_NaN();
        std::int64_t changes_ = 0;
        double gainSum_ = 0;
        double lossSum_ = 0;
        double avgGain_ = 0;
        double avgLoss_ = 0;
        double value_ = std::numeric_limits<double>::quiet_NaN();
    };

    /** MACD line, signal and histogram, matching the batch seeding exactly. */
    class Macd {
    public:
        Macd(int fastPeriod, int slowPeriod, int signalPeriod)
            : fast_(fastPeriod), slow_(slowPeriod), signalEma_(signalPeriod) {}

        /** Returns the MACD line (NaN during warm-up). */
        double update(double v);

        double line() const { return line_; }
        double signal() const { return signal_; }
        double histogram() const { return histogram_; }

    private:
        Ema fast_;
        Ema slow_;
        Ema signalEma_;
        double line_ = std::numeric_limits<double>::quiet_NaN();
        double signal_ = std::numeric_limits<double>::quiet_NaN();
        double histogram_ = std::numeric_limits<double>::quiet_NaN();
    };

    /** Cumulative volume-weighted average price. */
    class Vwap {
    public:
        double update(double price, double volume) {
            cumPv_ += price * volume;
            cumVol_ += volume;
            value_ = cumVol_ == 0 ? price : cumPv_ / cumVol_;
            return value_;
        }

        double value() const { return value_; }

    private:
        double cumPv_ = 0;
        double cumVol_ = 0;
        double value_ = std::numeric_limits<double>::quiet_NaN();
    };
};

}  // namespace quantfinlib
