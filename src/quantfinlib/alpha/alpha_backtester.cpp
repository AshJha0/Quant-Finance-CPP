#include "quantfinlib/alpha/alpha_backtester.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/backtest/performance_analytics.hpp"
#include "quantfinlib/backtest/trade.hpp"
#include "quantfinlib/backtest/volume_cap.hpp"
#include "quantfinlib/microstructure/market_impact_model.hpp"

namespace quantfinlib {
namespace alpha_backtester {

Config Config::defaults(int startIndex) {
    Config c;
    c.startIndex = startIndex;
    c.rebalanceEveryBars = 21;
    c.commissionBps = 1.0;
    c.halfSpreadBps = 2.0;
    c.slippageBps = 1.0;
    c.capital = 100'000'000;
    c.impactWindow = 20;
    c.periodsPerYear = 252;
    return c;
}

void validateConfig(const Config& config) {
    if (config.startIndex < 0 || config.rebalanceEveryBars <= 0 || config.commissionBps < 0
        || config.halfSpreadBps < 0 || config.slippageBps < 0 || config.capital < 0
        || config.impactWindow < 2 || config.periodsPerYear <= 0
        || !(config.maxParticipationRate >= 0 && config.maxParticipationRate <= 1)
        || !(config.borrowBpsAnnual >= 0) || !(config.financingBpsAnnual >= 0)) {
        throw std::invalid_argument("invalid backtest config");
    }
}

namespace {

double impactBps(const AlphaContext& ctx, int symbol, int index, double tradedFraction,
                 double currentCapital, const Config& config) {
    const BarSeries& s = ctx.series(symbol);
    std::optional<MarketImpactModel> impact = MarketImpactModel::estimate(s, index, config.impactWindow);
    if (!impact.has_value()) {
        return 0;
    }
    double shares = tradedFraction * currentCapital / s.close(index);
    return impact->squareRootImpactBps(shares);
}

}  // namespace

Result run(const AlphaContext& ctx, const AlphaFactor& factor, const Config& config) {
    return run(ctx, factor, config,
               [](const AlphaContext&, const std::vector<double>& scores, int) {
                   return portfolio_construction::zScoreWeights(scores, 1.0, 0.05);
               });
}

Result run(const AlphaContext& ctx, const AlphaFactor& factor, const Config& config,
          const WeightBuilder& builder) {
    validateConfig(config);
    int n = ctx.bars();
    int m = ctx.symbolCount();
    if (config.startIndex >= n - 1) {
        throw std::invalid_argument("startIndex leaves no bars to trade");
    }
    if (config.capital > 0 && config.startIndex < config.impactWindow) {
        throw std::invalid_argument("startIndex < impactWindow -- impact estimation needs that "
                                    "history (or set capital to 0 to disable impact)");
    }
    std::vector<double> weights(static_cast<std::size_t>(m), 0.0);
    std::vector<double> netEquity(static_cast<std::size_t>(n - config.startIndex));
    std::vector<double> grossEquity(netEquity.size());
    netEquity[0] = 1.0;
    grossEquity[0] = 1.0;
    double commissionDrag = 0;
    double spreadDrag = 0;
    double slippageDrag = 0;
    double impactDrag = 0;
    double borrowDrag = 0;
    double financingDrag = 0;
    double turnoverSum = 0;
    int rebalances = 0;

    const bool sameBar = config.fillTiming == FillTiming::SAME_BAR_CLOSE;
    std::vector<std::int64_t> timestamps(static_cast<std::size_t>(n));
    for (int t = 0; t < n; t++) {
        timestamps[static_cast<std::size_t>(t)] = ctx.timestamp(t);
    }
    const auto actions = corporate_actions::schedule(ctx.symbols(), timestamps, ctx.universeForEngine(),
                                                     config.delistings, config.mergers);
    constexpr int NO_EVENT = -2;  // matches no bar, nor the schedule's -1 for "none"
    std::vector<char> volumeKnown(static_cast<std::size_t>(m), 0);
    for (int i = 0; i < m; i++) {
        volumeKnown[static_cast<std::size_t>(i)] = volume_cap::hasVolumeData(ctx.series(i));
    }

    // Moves the book through one price leg that starts from bar `prevBar`'s
    // price. Weights are fractions of equity, so they drift; the rest of the
    // book is cash at zero return. A name whose terminal event falls on
    // `eventBar` pays its delisting return, or converts at the deal terms: the
    // cash leg leaves the book and the stock leg becomes acquirer weight,
    // valued at the acquirer's price at the end of this leg. Returns the
    // book's growth factor.
    auto drift = [&](int eventBar, int prevBar, const std::function<double(int)>& simpleReturn) {
        std::vector<double> returns(static_cast<std::size_t>(m), 0.0);
        std::vector<double> stockLeg(static_cast<std::size_t>(m), 0.0);
        double portfolioReturn = 0;
        for (int i = 0; i < m; i++) {
            auto idx = static_cast<std::size_t>(i);
            if (weights[idx] == 0) {
                continue;
            }
            const corporate_actions::TerminalAction& action = actions[idx];
            if (action.bar != eventBar) {
                returns[idx] = simpleReturn(i);
            } else if (!action.merger) {
                returns[idx] = action.delistingReturn;
            } else {
                double lastClose = ctx.series(i).close(prevBar);
                double stockValue = 0;
                if (action.acquirerSharesPerShare > 0) {
                    int a = action.acquirerIndex;
                    stockValue = action.acquirerSharesPerShare * ctx.series(a).close(prevBar)
                            * (1 + simpleReturn(a));
                    stockLeg[static_cast<std::size_t>(a)] += weights[idx] * stockValue / lastClose;
                }
                returns[idx] = (action.cashPerShare + stockValue) / lastClose - 1;
            }
            portfolioReturn += weights[idx] * returns[idx];
        }
        double growth = 1 + portfolioReturn;
        if (!(growth > 0)) {
            throw std::invalid_argument("book wiped out");
        }
        for (int i = 0; i < m; i++) {
            auto idx = static_cast<std::size_t>(i);
            weights[idx] = actions[idx].bar == eventBar
                    ? 0
                    : (weights[idx] * (1 + returns[idx]) + stockLeg[idx]) / growth;
        }
        return growth;
    };

    // Trades toward target at `execBar`, sizing impact from what was known
    // at `decisionBar`. Returns the fraction of equity kept after costs.
    auto rebalance = [&](const std::vector<double>& target, int decisionBar, int execBar,
                         bool atOpen, double equityNow) {
        double currentCapital = config.capital * equityNow;
        double turnover = 0;
        double commission = 0;
        double spread = 0;
        double slip = 0;
        double impact = 0;
        for (int i = 0; i < m; i++) {
            auto idx = static_cast<std::size_t>(i);
            bool gone = actions[idx].goneBy(execBar);
            double wanted = (gone ? 0.0 : target[idx]) - weights[idx];
            double traded = std::abs(wanted);
            if (traded == 0) {
                continue;
            }
            if (currentCapital > 0) {
                const BarSeries& s = ctx.series(i);
                double px = atOpen ? s.open(execBar) : s.close(execBar);
                double maxShares = volume_cap::maxQuantity(s, execBar, config.maxParticipationRate,
                                                           volumeKnown[idx] != 0);
                traded = std::min(traded, maxShares * px / currentCapital);
                if (traded == 0) {
                    continue;
                }
            }
            weights[idx] += wanted > 0 ? traded : -traded;
            turnover += traded;
            commission += traded * config.commissionBps / 1e4;
            spread += traded * config.halfSpreadBps / 1e4;
            slip += traded * config.slippageBps / 1e4;
            if (config.capital > 0) {
                impact += traded * impactBps(ctx, i, decisionBar, traded, currentCapital, config) / 1e4;
            }
        }
        double cost = commission + spread + slip + impact;
        if (cost >= 1) {
            char buf[256];
            std::snprintf(buf, sizeof(buf),
                          "execution cost %.1f%% of equity at bar %d (impact %.1f%%) -- the "
                          "book is too large for this universe's liquidity; reduce capital or "
                          "the rebalance size",
                          cost * 100, execBar, impact * 100);
            throw std::invalid_argument(buf);
        }
        commissionDrag += commission;
        spreadDrag += spread;
        slippageDrag += slip;
        impactDrag += impact;
        turnoverSum += turnover / 2;
        rebalances++;
        return 1 - cost;
    };

    for (int t = config.startIndex; t < n - 1; t++) {
        int outIdx = t - config.startIndex;
        double equityNow = netEquity[static_cast<std::size_t>(outIdx)];
        double costFactor = 1.0;
        double growth = 1.0;
        bool due = outIdx % config.rebalanceEveryBars == 0;
        std::vector<double> target;

        if (due) {
            target = builder(ctx, factor.scores(ctx, t), t);
            if (static_cast<int>(target.size()) != m) {
                throw std::invalid_argument("weight builder returned misaligned array");
            }
            for (double w : target) {
                if (!std::isfinite(w)) {
                    throw std::invalid_argument("weight builder returned a non-finite weight");
                }
            }
        }

        if (sameBar) {
            if (due) {
                costFactor = rebalance(target, t, t, false, equityNow);
            }
            growth = drift(t + 1, t, [&](int i) { return ctx.returnOver(i, t, t + 1); });
        } else {
            growth = drift(t + 1, t, [&](int i) {
                const BarSeries& s = ctx.series(i);
                return s.open(t + 1) / s.close(t) - 1;
            });
            if (due) {
                costFactor = rebalance(target, t, t + 1, true, equityNow * growth);
            }
            growth *= drift(NO_EVENT, t + 1, [&](int i) {
                const BarSeries& s = ctx.series(i);
                return s.close(t + 1) / s.open(t + 1) - 1;
            });
        }

        // Carry on the book held into the next bar.
        double shortWeight = 0;
        double netWeight = 0;
        for (double w : weights) {
            netWeight += w;
            shortWeight += w < 0 ? -w : 0;
        }
        double borrow = shortWeight * config.borrowBpsAnnual / 1e4 / config.periodsPerYear;
        double financing = std::max(0.0, netWeight - 1) * config.financingBpsAnnual / 1e4
                / config.periodsPerYear;
        borrowDrag += borrow;
        financingDrag += financing;

        netEquity[static_cast<std::size_t>(outIdx + 1)] =
                equityNow * costFactor * growth * (1 - borrow - financing);
        grossEquity[static_cast<std::size_t>(outIdx + 1)] =
                grossEquity[static_cast<std::size_t>(outIdx)] * growth;
    }

    Result result;
    result.netEquity = netEquity;
    result.grossEquity = grossEquity;
    result.netMetrics = performance_analytics::compute(netEquity, {}, config.periodsPerYear);
    result.grossMetrics = performance_analytics::compute(grossEquity, {}, config.periodsPerYear);
    result.commissionDrag = commissionDrag;
    result.spreadDrag = spreadDrag;
    result.slippageDrag = slippageDrag;
    result.impactDrag = impactDrag;
    result.borrowDrag = borrowDrag;
    result.financingDrag = financingDrag;
    result.meanTurnover = rebalances == 0 ? 0 : turnoverSum / rebalances;
    return result;
}

}  // namespace alpha_backtester
}  // namespace quantfinlib
