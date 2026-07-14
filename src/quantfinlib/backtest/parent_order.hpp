// Port of com.quantfinlib.backtest.ParentOrder (Java record): one parent
// order worked by the ExecutionAwareBacktester — the signal that created
// it, the arrival price (close at signal time, the TCA benchmark), and
// the child fills with the bar index each filled on. reason is "ENTRY"
// for entries, or the exit reason (SIGNAL / STOP_LOSS / TAKE_PROFIT /
// END_OF_DATA).
#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include "quantfinlib/microstructure/execution.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct ParentOrder {
    static constexpr const char* REASON_ENTRY = "ENTRY";

    Side side = Side::BUY;
    int signalIndex = 0;
    double arrivalPrice = 0;
    std::string reason;
    std::vector<Execution> fills;
    std::vector<int> fillBarIndices;

    std::int64_t filledQty() const {
        std::int64_t qty = 0;
        for (const Execution& f : fills) {
            qty += f.quantity;
        }
        return qty;
    }

    double avgFillPrice() const {
        std::int64_t qty = 0;
        double notional = 0;
        for (const Execution& f : fills) {
            qty += f.quantity;
            notional += f.notional();
        }
        return qty == 0 ? std::numeric_limits<double>::quiet_NaN()
                        : notional / static_cast<double>(qty);
    }

    /** Bars from first to last fill (0 for unfilled or single-bar parents). */
    int fillDurationBars() const {
        return fills.empty() ? 0 : fillBarIndices.back() - fillBarIndices.front();
    }
};

}  // namespace quantfinlib
