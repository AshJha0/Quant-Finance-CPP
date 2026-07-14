// Port of com.quantfinlib.backtest.Trade (Java record): a completed
// round-trip trade. pnl is net of commissions; returnPct is relative to
// capital committed at entry. The record's accessors become public
// fields; isWin()/barsHeld() are kept verbatim.
#pragma once

#include <cstdint>
#include <string>

namespace quantfinlib {

struct Trade {
    static constexpr const char* REASON_SIGNAL = "SIGNAL";
    static constexpr const char* REASON_STOP_LOSS = "STOP_LOSS";
    static constexpr const char* REASON_TAKE_PROFIT = "TAKE_PROFIT";
    static constexpr const char* REASON_END_OF_DATA = "END_OF_DATA";

    std::string symbol;
    int entryIndex = 0;
    int exitIndex = 0;
    std::int64_t entryTime = 0;
    std::int64_t exitTime = 0;
    double entryPrice = 0;
    double exitPrice = 0;
    double quantity = 0;
    double pnl = 0;
    double returnPct = 0;
    std::string exitReason;

    bool isWin() const { return pnl > 0; }
    int barsHeld() const { return exitIndex - entryIndex; }
};

}  // namespace quantfinlib
