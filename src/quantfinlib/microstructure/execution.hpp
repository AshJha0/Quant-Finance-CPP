// Port of com.quantfinlib.microstructure.Execution (Java record): a
// matched trade (fill) for TCA and venue analytics.
#pragma once

#include <cstdint>
#include <string>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct Execution {
    std::string symbol;
    Side side = Side::BUY;
    double price = 0;
    std::int64_t quantity = 0;
    std::int64_t timestampNanos = 0;
    std::string venue;

    double notional() const { return price * static_cast<double>(quantity); }
};

}  // namespace quantfinlib
