// Port of com.quantfinlib.core.BarSeries: immutable, cache-friendly OHLCV
// time series in structure-of-arrays layout. The Java zero-copy array
// accessors (closes(), opens(), ...) become const references to the
// internal vectors. slice() copies [from, toExclusive) for train/test
// splits; of() builds a closes-only series (open = high = low = close,
// volume 1e6, daily timestamps) exactly like the Java helper. Gates map
// IllegalArgumentException -> std::invalid_argument and the empty-build
// IllegalStateException -> std::logic_error.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/data/bar.hpp"

namespace quantfinlib {

class BarSeries {
public:
    class Builder {
    public:
        explicit Builder(std::string symbol) : symbol_(std::move(symbol)) {}

        Builder& add(std::int64_t timestamp, double open, double high, double low,
                     double close, double volume);

        Builder& add(const Bar& bar);

        BarSeries build() const;

    private:
        std::string symbol_;
        std::vector<std::int64_t> ts_;
        std::vector<double> o_, h_, l_, c_, v_;
    };

    static Builder builder(std::string symbol) { return Builder(std::move(symbol)); }

    /** Builds a series from close prices only (open = high = low = close). */
    static BarSeries of(const std::string& symbol, const std::vector<double>& closes);

    static BarSeries fromBars(const std::string& symbol, const std::vector<Bar>& bars);

    const std::string& symbol() const { return symbol_; }
    int size() const { return static_cast<int>(close_.size()); }
    std::int64_t timestamp(int i) const { return timestamps_[static_cast<std::size_t>(i)]; }
    double open(int i) const { return open_[static_cast<std::size_t>(i)]; }
    double high(int i) const { return high_[static_cast<std::size_t>(i)]; }
    double low(int i) const { return low_[static_cast<std::size_t>(i)]; }
    double close(int i) const { return close_[static_cast<std::size_t>(i)]; }
    double volume(int i) const { return volume_[static_cast<std::size_t>(i)]; }
    double lastClose() const { return close_.back(); }
    Bar bar(int i) const {
        return Bar(timestamp(i), open(i), high(i), low(i), close(i), volume(i));
    }

    /** Zero-copy accessors: internal vectors, treat as read-only. */
    const std::vector<std::int64_t>& timestamps() const { return timestamps_; }
    const std::vector<double>& opens() const { return open_; }
    const std::vector<double>& highs() const { return high_; }
    const std::vector<double>& lows() const { return low_; }
    const std::vector<double>& closes() const { return close_; }
    const std::vector<double>& volumes() const { return volume_; }

    /** Copy of the bar range [from, toExclusive) as a new series (for train/test splits). */
    BarSeries slice(int from, int toExclusive) const;

    /** Simple (arithmetic) returns; length = size - 1. */
    std::vector<double> returns() const;

    /** Log returns; length = size - 1. */
    std::vector<double> logReturns() const;

private:
    BarSeries(std::string symbol, std::vector<std::int64_t> timestamps,
              std::vector<double> open, std::vector<double> high, std::vector<double> low,
              std::vector<double> close, std::vector<double> volume);

    std::string symbol_;
    std::vector<std::int64_t> timestamps_;
    std::vector<double> open_, high_, low_, close_, volume_;
};

}  // namespace quantfinlib
