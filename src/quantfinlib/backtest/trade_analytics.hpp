// Port of com.quantfinlib.backtest.TradeAnalytics: trade-level statistics
// (expectancy, payoff ratio, streaks, Kelly, hold times). Scratch trades
// (pnl == 0) break both streaks and belong to neither leg; a record with
// no losers reports payoff = +inf and Kelly clamped to 1 (the over-fit
// tell); Kelly is clamped to [0, 1] otherwise. Non-finite pnl and empty
// input throw std::invalid_argument (IllegalArgumentException in Java).
#pragma once

#include <vector>

#include "quantfinlib/backtest/trade.hpp"

namespace quantfinlib {
namespace trade_analytics {

struct Result {
    int count = 0;                    // number of trades
    double winRate = 0;               // fraction of strictly-positive trades
    double expectancy = 0;            // average P&L per trade
    double avgWin = 0;                // mean P&L of winning trades (0 if none)
    double avgLoss = 0;               // mean MAGNITUDE of losing trades (0 if none)
    double payoffRatio = 0;           // avgWin / avgLoss (+inf if no losers)
    int maxWinStreak = 0;             // longest run of consecutive winners
    int maxLossStreak = 0;            // longest run of consecutive losers
    double kellyFraction = 0;         // W - (1-W)/R, clamped to [0, 1]
    double avgBarsHeldWinners = 0;    // mean holding period of winners
    double avgBarsHeldLosers = 0;     // mean holding period of losers
};

/** @param trades completed trades, non-empty. */
Result analyze(const std::vector<Trade>& trades);

}  // namespace trade_analytics
}  // namespace quantfinlib
