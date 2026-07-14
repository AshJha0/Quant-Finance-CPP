// Port of com.quantfinlib.orderbook.Side (Java): order side with the
// signed-arithmetic helper used by risk gates and slippage math.
#pragma once

namespace quantfinlib {

enum class Side { BUY, SELL };

/** +1 for BUY, -1 for SELL -- for signed cost/slippage arithmetic. */
inline int sign(Side s) {
    return s == Side::BUY ? 1 : -1;
}

inline Side opposite(Side s) {
    return s == Side::BUY ? Side::SELL : Side::BUY;
}

}  // namespace quantfinlib
