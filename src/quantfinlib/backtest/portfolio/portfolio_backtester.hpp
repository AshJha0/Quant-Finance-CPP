// Port of com.quantfinlib.backtest.portfolio.PortfolioBacktester
// (classic overload): multi-asset, long/short portfolio backtester —
// rebalances positions (possibly fractional and negative) toward the
// strategy's target weights at a configurable cadence, charging
// commission on traded notional. This is where the optimization stack
// meets the backtester — feed optimizer weights, vol-target overlays,
// or momentum rankings straight in.
//
// PORT SCOPE: the Java survivorship-aware overload (PointInTimeUniverse
// membership, delistings/mergers/index drops, cash dividends) is NOT
// ported — its dependencies (com.quantfinlib.data.PointInTimeUniverse,
// CorporateActions) have no C++ port yet. This is the classic run:
// every supplied symbol is tradeable on every bar. The Result fields
// populated only by that overload (dividendCashCredited,
// lifecycleEventsApplied) are omitted. Gates map to
// std::invalid_argument.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "quantfinlib/backtest/performance_metrics.hpp"
#include "quantfinlib/backtest/portfolio/portfolio_strategy.hpp"
#include "quantfinlib/backtest/trade_cost_model.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {
namespace portfolio_backtester {

/**
 * costModel, when set (non-empty std::function), supersedes the flat
 * commissionRate for every trade — the shared TradeCostModel seam that
 * makes a run execution-aware. An empty costModel keeps the legacy flat
 * commission behavior exactly (Java's null).
 */
struct Config {
    double initialCapital = 0;
    double commissionRate = 0;
    int rebalanceEveryBars = 1;
    int periodsPerYear = 252;
    TradeCostModel costModel;  // empty = flat commissionRate

    static Config defaults() { return Config{1000000, 0.001, 1, 252, nullptr}; }

    Config withRebalanceEvery(int bars) const {
        Config c = *this;
        c.rebalanceEveryBars = bars;
        return c;
    }

    /** Pluggable per-trade costs (e.g. trade_cost_model::institutional). */
    Config withCostModel(TradeCostModel model) const {
        Config c = *this;
        c.costModel = std::move(model);
        return c;
    }

    /** One-way trade fee for `notional` of `series` at bar `index`. */
    double fee(const BarSeries& series, int index, double notional) const {
        return costModel ? notional * costModel(series, index, notional) / 1e4
                         : notional * commissionRate;
    }
};

struct Result {
    std::vector<double> equityCurve;
    PerformanceMetrics metrics;
    double totalCosts = 0;
    double totalTurnoverNotional = 0;
    std::map<std::string, double> finalPositions;
};

/** Classic run: every supplied symbol is tradeable on every bar. */
Result run(PortfolioStrategy& strategy, const std::map<std::string, BarSeries>& data,
           const Config& config);

}  // namespace portfolio_backtester
}  // namespace quantfinlib
