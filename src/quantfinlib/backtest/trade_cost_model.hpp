// Port of com.quantfinlib.backtest.TradeCostModel (Java functional
// interface): a pluggable per-trade cost model — the ONE definition of
// "what a trade costs" shared by the backtest engines, so an
// execution-aware number and a survivorship-aware number can come out
// of the SAME run:
//
//  - flat(): a fixed all-in bps (the classic commission assumption, and
//    the exact equivalent of the legacy commissionRate configs);
//  - institutional(): commission + half-spread + slippage + square-root
//    market impact, with per-symbol ADV/vol estimated from the trailing
//    bars via MarketImpactModel::estimate. The impact term is what makes
//    cost grow with book size, i.e. what turns "capacity" into a number.
//
// The contract prices ONE side of a trade (a buy or a sell), all-in, in
// basis points of traded notional. Implementations must be pure
// functions of their arguments — engines may call them at any bar in
// any order. The Java @FunctionalInterface becomes a std::function
// alias; the static factories live in the trade_cost_model namespace.
// Gates map to std::invalid_argument.
#pragma once

#include <functional>

#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {

/**
 * All-in one-way cost, in bps of traded notional, for trading `notional`
 * (currency units, always positive) of `series` at bar `index`.
 */
using TradeCostModel = std::function<double(const BarSeries& series, int index, double notional)>;

namespace trade_cost_model {

/** Fixed all-in bps per trade — commission-only, size-independent. */
TradeCostModel flat(double bps);

/**
 * The institutional four-component model. Impact needs trailing ADV/vol:
 * bars before impactWindow charge the flat components only (rather than
 * reading before bar 0), as do series without volume data — documented
 * degradation, never a crash.
 *
 * @param commissionBps commission per side
 * @param halfSpreadBps half the quoted spread, paid on every trade
 * @param slippageBps   fixed implementation noise
 * @param impactWindow  trailing bars for ADV/vol estimation (>= 2)
 */
TradeCostModel institutional(double commissionBps, double halfSpreadBps, double slippageBps,
                             int impactWindow);

}  // namespace trade_cost_model
}  // namespace quantfinlib
