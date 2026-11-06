// Port of com.quantfinlib.backtest.portfolio.PortfolioBacktester
// (classic overload): multi-asset, long/short portfolio backtester —
// rebalances positions (possibly fractional and negative) toward the
// strategy's target weights at a configurable cadence, charging
// commission on traded notional. Weights decided on bar t's close trade at
// bar t+1's open unless fillTiming says otherwise, each trade capped at
// maxParticipationRate of the bar's volume. Shorts pay borrow and a cash
// debit pays financing, accrued per bar. Delistings close at the recorded
// delisting return. This is where the optimization stack
// meets the backtester — feed optimizer weights, vol-target overlays,
// or momentum rankings straight in.
//
// PORT SCOPE: the Java survivorship-aware overload (PointInTimeUniverse
// membership, mergers/index drops, cash dividends) is NOT ported; only
// Config::delistings is. Otherwise every supplied symbol is tradeable on
// every bar. The Result fields
// populated only by that overload (dividendCashCredited,
// lifecycleEventsApplied) are omitted. Gates map to
// std::invalid_argument.
#pragma once

#include <map>
#include <string>
#include <vector>

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/delisting.hpp"
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
    double commissionRate = 0.001;
    int rebalanceEveryBars = 1;
    int periodsPerYear = 252;
    TradeCostModel costModel;  // empty = flat commissionRate
    FillTiming fillTiming = FillTiming::NEXT_BAR_OPEN;
    double maxParticipationRate = 0.10;  // of bar volume; 0 disables
    // Annual rates, accrued per bar. Assumed levels, not market data: 50 bps
    // is general-collateral borrow; hard-to-borrow names cost far more.
    double borrowRateAnnual = 0.005;     // on short market value
    double financingRateAnnual = 0.05;   // on a negative cash balance
    std::vector<Delisting> delistings;

    static Config defaults() {
        Config c;
        c.initialCapital = 1000000;
        return c;
    }

    /** Close-of-signal fills, no volume cap, no carry: the textbook idealisation. */
    Config withIdealisedExecution() const {
        Config c = *this;
        c.fillTiming = FillTiming::SAME_BAR_CLOSE;
        c.maxParticipationRate = 0;
        c.borrowRateAnnual = 0;
        c.financingRateAnnual = 0;
        return c;
    }

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
    double totalBorrowCost = 0;
    double totalFinancingCost = 0;
};

/** Classic run: every supplied symbol is tradeable on every bar. */
Result run(PortfolioStrategy& strategy, const std::map<std::string, BarSeries>& data,
           const Config& config);

}  // namespace portfolio_backtester
}  // namespace quantfinlib
