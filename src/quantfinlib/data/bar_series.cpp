#include "quantfinlib/data/bar_series.hpp"

#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

namespace quantfinlib {

BarSeries::Builder& BarSeries::Builder::add(std::int64_t timestamp, double open, double high,
                                            double low, double close, double volume) {
    ts_.push_back(timestamp);
    o_.push_back(open);
    h_.push_back(high);
    l_.push_back(low);
    c_.push_back(close);
    v_.push_back(volume);
    return *this;
}

BarSeries::Builder& BarSeries::Builder::add(const Bar& bar) {
    return add(bar.timestamp(), bar.open(), bar.high(), bar.low(), bar.close(), bar.volume());
}

BarSeries BarSeries::Builder::build() const {
    if (ts_.empty()) {
        throw std::logic_error("empty series: " + symbol_);
    }
    return BarSeries(symbol_, ts_, o_, h_, l_, c_, v_);
}

BarSeries BarSeries::of(const std::string& symbol, const std::vector<double>& closes) {
    Builder b(symbol);
    for (std::size_t i = 0; i < closes.size(); i++) {
        b.add(static_cast<std::int64_t>(i) * 86'400'000LL, closes[i], closes[i], closes[i],
              closes[i], 1'000'000);
    }
    return b.build();
}

BarSeries BarSeries::fromBars(const std::string& symbol, const std::vector<Bar>& bars) {
    Builder b(symbol);
    for (const Bar& bar : bars) {
        b.add(bar);
    }
    return b.build();
}

BarSeries::BarSeries(std::string symbol, std::vector<std::int64_t> timestamps,
                     std::vector<double> open, std::vector<double> high, std::vector<double> low,
                     std::vector<double> close, std::vector<double> volume)
        : symbol_(std::move(symbol)),
          timestamps_(std::move(timestamps)),
          open_(std::move(open)),
          high_(std::move(high)),
          low_(std::move(low)),
          close_(std::move(close)),
          volume_(std::move(volume)) {}

BarSeries BarSeries::slice(int from, int toExclusive) const {
    if (from < 0 || toExclusive > size() || from >= toExclusive) {
        throw std::invalid_argument("bad slice [" + std::to_string(from) + ","
                                    + std::to_string(toExclusive) + ") of "
                                    + std::to_string(size()));
    }
    Builder b(symbol_);
    for (int i = from; i < toExclusive; i++) {
        b.add(timestamp(i), open(i), high(i), low(i), close(i), volume(i));
    }
    return b.build();
}

std::vector<double> BarSeries::returns() const {
    std::vector<double> r(static_cast<std::size_t>(size() - 1));
    for (int i = 1; i < size(); i++) {
        r[static_cast<std::size_t>(i - 1)] = close(i) / close(i - 1) - 1.0;
    }
    return r;
}

std::vector<double> BarSeries::logReturns() const {
    std::vector<double> r(static_cast<std::size_t>(size() - 1));
    for (int i = 1; i < size(); i++) {
        r[static_cast<std::size_t>(i - 1)] = std::log(close(i) / close(i - 1));
    }
    return r;
}

}  // namespace quantfinlib
