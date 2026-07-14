#include "quantfinlib/alpha/alpha_backtester.hpp"

#include <cmath>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <string>

#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/backtest/performance_analytics.hpp"
#include "quantfinlib/backtest/trade.hpp"
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
        || config.impactWindow < 2 || config.periodsPerYear <= 0) {
        throw std::invalid_argument("invalid backtest config");
    }
}

namespace {

double impactBps(const AlphaContext& ctx, int symbol, int index, double tradedFraction,
                 const Config& config) {
    const BarSeries& s = ctx.series(symbol);
    std::optional<MarketImpactModel> impact = MarketImpactModel::estimate(s, index, config.impactWindow);
    if (!impact.has_value()) {
        return 0;
    }
    double shares = tradedFraction * config.capital / s.close(index);
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
    double turnoverSum = 0;
    int rebalances = 0;

    for (int t = config.startIndex; t < n - 1; t++) {
        int outIdx = t - config.startIndex;
        double costFactor = 1.0;

        if (outIdx % config.rebalanceEveryBars == 0) {
            std::vector<double> target = builder(ctx, factor.scores(ctx, t), t);
            if (static_cast<int>(target.size()) != m) {
                throw std::invalid_argument("weight builder returned misaligned array");
            }
            double turnover = 0;
            double commission = 0;
            double spread = 0;
            double slip = 0;
            double impact = 0;
            for (int i = 0; i < m; i++) {
                auto idx = static_cast<std::size_t>(i);
                double traded = std::abs(target[idx] - weights[idx]);
                if (traded == 0) {
                    continue;
                }
                turnover += traded;
                commission += traded * config.commissionBps / 1e4;
                spread += traded * config.halfSpreadBps / 1e4;
                slip += traded * config.slippageBps / 1e4;
                if (config.capital > 0) {
                    impact += traded * impactBps(ctx, i, t, traded, config) / 1e4;
                }
            }
            double cost = commission + spread + slip + impact;
            if (cost >= 1) {
                char buf[256];
                std::snprintf(buf, sizeof(buf),
                              "execution cost %.1f%% of equity at bar %d (impact %.1f%%) -- the "
                              "book is too large for this universe's liquidity; reduce capital or "
                              "the rebalance size",
                              cost * 100, t, impact * 100);
                throw std::invalid_argument(buf);
            }
            costFactor = 1 - cost;
            commissionDrag += commission;
            spreadDrag += spread;
            slippageDrag += slip;
            impactDrag += impact;
            turnoverSum += turnover / 2;
            rebalances++;
            weights = target;
        }

        double portfolioReturn = 0;
        for (int i = 0; i < m; i++) {
            auto idx = static_cast<std::size_t>(i);
            if (weights[idx] != 0) {
                portfolioReturn += weights[idx] * ctx.returnOver(i, t, t + 1);
            }
        }
        netEquity[static_cast<std::size_t>(outIdx + 1)] =
                netEquity[static_cast<std::size_t>(outIdx)] * costFactor * (1 + portfolioReturn);
        grossEquity[static_cast<std::size_t>(outIdx + 1)] =
                grossEquity[static_cast<std::size_t>(outIdx)] * (1 + portfolioReturn);
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
    result.meanTurnover = rebalances == 0 ? 0 : turnoverSum / rebalances;
    return result;
}

}  // namespace alpha_backtester
}  // namespace quantfinlib
