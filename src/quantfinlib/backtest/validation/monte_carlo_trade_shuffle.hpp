// Port of com.quantfinlib.backtest.validation.MonteCarloTradeShuffle:
// reshuffles the same trade P&Ls into thousands of random sequences to
// get the distribution of max drawdowns and terminal wealth the trade
// set implies — isolating path/sequencing risk from edge. Drawdown is on
// the cumulative-P&L path (additive, no starting-capital assumption).
// Fisher-Yates on the Java-exact SplittableRandom, so seeded results are
// bit-identical to the Java library. Gates: >= 2 trades, >= 100
// shuffles, finite pnl (std::invalid_argument).
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/backtest/trade.hpp"

namespace quantfinlib {
namespace monte_carlo_trade_shuffle {

struct Result {
    double medianMaxDrawdown = 0;  // 50th-pct max drawdown across shuffles (P&L units)
    double p95MaxDrawdown = 0;     // the plan-for number
    double p99MaxDrawdown = 0;     // the tail
    double medianTerminalPnl = 0;  // 50th-pct terminal cumulative P&L
    double probLoss = 0;           // fraction of shuffles ending below zero
    double actualMaxDrawdown = 0;  // the observed (unshuffled) path's max drawdown
    double actualDrawdownPct = 0;  // observed drawdown's rank in the distribution (0..1)

    bool operator==(const Result&) const = default;
};

/** @param trades the strategy's completed trades, >= 2; @param shuffles >= 100. */
Result analyze(const std::vector<Trade>& trades, int shuffles, std::int64_t seed);

}  // namespace monte_carlo_trade_shuffle
}  // namespace quantfinlib
