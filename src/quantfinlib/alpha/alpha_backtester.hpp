// Port of com.quantfinlib.alpha.AlphaBacktester: execution-aware factor
// backtest -- runs a factor through a construction pipeline into weights,
// holds between rebalances, and charges commission + half-spread +
// slippage + sqrt-law market impact (via MarketImpactModel). Weight-based
// simulation (fractional book, multiplicative compounding). Cost >= 100%
// of equity at a rebalance throws (the capacity answer).
//
// DEVIATION FROM JAVA, all so that a run cannot flatter the factor:
//  - Weights decided on bar t's close trade at bar t+1's open.
//  - Weights drift with prices between rebalances, so a rebalance pays to
//    get back to target rather than being free.
//  - Impact and the volume cap size trades from current capital
//    (capital * net equity), not the starting book.
//  - Each name's trade is capped at maxParticipationRate of the execution
//    bar's volume; an unreached target is left for the next rebalance.
//  - Shorts pay borrow, net long above 1 pays financing.
//  - Delisted names pay their delisting return and leave the book. Events
//    come from Config::delistings and the context's universe; mergers are
//    not modelled.
#pragma once

#include <functional>
#include <vector>

#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"
#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/delisting.hpp"
#include "quantfinlib/backtest/performance_metrics.hpp"

namespace quantfinlib {
namespace alpha_backtester {

/** Builds target weights from raw scores at a rebalance (the construction hook). */
using WeightBuilder =
        std::function<std::vector<double>(const AlphaContext&, const std::vector<double>&, int)>;

/**
 * @param startIndex         first tradeable bar (must cover factor warm-up and the impact window)
 * @param rebalanceEveryBars  holding period between weight refreshes
 * @param commissionBps       commission per side on traded notional
 * @param halfSpreadBps       half the quoted spread, paid per trade
 * @param slippageBps         fixed implementation noise per trade
 * @param capital             book size in currency (0 disables the impact model)
 * @param impactWindow        trailing bars for ADV/vol estimation
 * @param periodsPerYear      bar frequency for annualized metrics
 * @param fillTiming          when a rebalance decided at a close trades
 * @param maxParticipationRate cap per trade as a share of bar volume (0 disables; needs capital > 0)
 * @param borrowBpsAnnual     assumed borrow on short weight (general collateral)
 * @param financingBpsAnnual  assumed financing on net long above 1
 * @param delistings          terminal events, added to the universe's
 */
struct Config {
    int startIndex = 0;
    int rebalanceEveryBars = 21;
    double commissionBps = 1.0;
    double halfSpreadBps = 2.0;
    double slippageBps = 1.0;
    double capital = 100'000'000;
    int impactWindow = 20;
    int periodsPerYear = 252;
    FillTiming fillTiming = FillTiming::NEXT_BAR_OPEN;
    double maxParticipationRate = 0.10;
    double borrowBpsAnnual = 50;
    double financingBpsAnnual = 500;
    std::vector<Delisting> delistings;

    /** Close-of-signal fills, no volume cap, no carry: the textbook idealisation. */
    Config withIdealisedExecution() const {
        Config c = *this;
        c.fillTiming = FillTiming::SAME_BAR_CLOSE;
        c.maxParticipationRate = 0;
        c.borrowBpsAnnual = 0;
        c.financingBpsAnnual = 0;
        return c;
    }

    /** Institutional-ish daily-bar defaults. */
    static Config defaults(int startIndex);
};

void validateConfig(const Config& config);

/** Net/gross curves plus the cumulative fraction of equity each cost component consumed. */
struct Result {
    std::vector<double> netEquity;
    std::vector<double> grossEquity;
    PerformanceMetrics netMetrics;
    PerformanceMetrics grossMetrics;
    double commissionDrag = 0;
    double spreadDrag = 0;
    double slippageDrag = 0;
    double impactDrag = 0;
    double borrowDrag = 0;
    double financingDrag = 0;
    double meanTurnover = 0;

    /** Total cost drag: gross minus net, decomposed. */
    double totalCostDrag() const {
        return commissionDrag + spreadDrag + slippageDrag + impactDrag + borrowDrag + financingDrag;
    }
};

/** Runs with the standard z-score construction (gross 1.0, 5% name cap). */
Result run(const AlphaContext& ctx, const AlphaFactor& factor, const Config& config);

/** Runs with a caller-supplied construction pipeline. */
Result run(const AlphaContext& ctx, const AlphaFactor& factor, const Config& config,
           const WeightBuilder& builder);

}  // namespace alpha_backtester
}  // namespace quantfinlib
