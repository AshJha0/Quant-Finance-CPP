// Port of com.quantfinlib.core.Bar (Java record): immutable OHLCV bar.
// Timestamp is epoch milliseconds. The record's compact constructor
// validates exactly one invariant -- high >= low -- and throws
// IllegalArgumentException (std::invalid_argument here) otherwise.
#pragma once

#include <cstdint>

namespace quantfinlib {

class Bar {
public:
    Bar(std::int64_t timestamp, double open, double high, double low,
        double close, double volume);

    std::int64_t timestamp() const { return timestamp_; }
    double open() const { return open_; }
    double high() const { return high_; }
    double low() const { return low_; }
    double close() const { return close_; }
    double volume() const { return volume_; }

    double typicalPrice() const;
    double range() const;
    bool isBullish() const;

private:
    std::int64_t timestamp_;
    double open_;
    double high_;
    double low_;
    double close_;
    double volume_;
};

}  // namespace quantfinlib
