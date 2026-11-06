// Port of com.quantfinlib.backtest.Backtester: event-driven,
// single-instrument, long-only backtesting engine.
//
// Execution model: a signal decided on bar t's close fills at bar t+1's
// open, adjusted for slippage (config.fillTiming opts in to the signal
// bar's close). A signal on the last bar has no next bar and is dropped;
// any open position is force-closed at the last close. Stop-loss /
// take-profit levels are evaluated intrabar against the bar's low/high,
// with gap-aware fills (a gap through the level fills at the open); stops
// pay slippage. Each fill is capped at config.maxParticipationRate of the
// bar's volume: a capped entry keeps the unspent cash, a capped exit
// carries its remainder to the next open, and each part is its own Trade.
// The forced final close ignores the cap. Commission is charged on both
// entry and exit notional. The Java static utility class becomes a
// namespace; IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/backtest_result.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {
namespace backtester {

BacktestResult run(TradingStrategy& strategy, const BarSeries& series,
                   const BacktestConfig& config);

/**
 * Variant with a WARM-UP prefix: indicators are initialized over the
 * whole series, but no signal is acted on (and no equity recorded)
 * before tradeFrom. This is how walk-forward analysis avoids the
 * cold-start bias — evaluating a fold on a bare test slice re-computes
 * every indicator from scratch, silently forcing HOLD through the first
 * lookback bars of EVERY fold; feeding the preceding bars as warm-up
 * (they are the past — no look-ahead) lets the strategy enter the test
 * window with warm indicators, the way it would trade live. The
 * returned equity curve covers [tradeFrom, n) only.
 *
 * Scope of the warm-up: it warms whatever TradingStrategy::init
 * precomputes over the series (all shipped strategies). onBar is NOT
 * called for warm-up bars, so a strategy that accumulates state inside
 * onBar still starts cold at tradeFrom.
 */
BacktestResult run(TradingStrategy& strategy, const BarSeries& series,
                   const BacktestConfig& config, int tradeFrom);

}  // namespace backtester
}  // namespace quantfinlib
