#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"

#include <cmath>
#include <stdexcept>

#include "quantfinlib/backtest/performance_analytics.hpp"

namespace quantfinlib {
namespace portfolio_backtester {

namespace {

double marketValue(const std::map<std::string, double>& positions,
                   const std::map<std::string, BarSeries>& data, int index) {
    double value = 0;
    for (const auto& p : positions) {
        value += p.second * data.at(p.first).close(index);
    }
    return value;
}

}  // namespace

Result run(PortfolioStrategy& strategy, const std::map<std::string, BarSeries>& data,
           const Config& config) {
    if (data.empty()) {
        throw std::invalid_argument("no series supplied");
    }
    // std::map iterates in sorted key order — the same determinism the
    // Java engine gets by sorting the symbol list.
    int n = data.begin()->second.size();
    for (const auto& e : data) {
        if (e.second.size() != n) {
            throw std::invalid_argument("series must be index-aligned: " + e.first);
        }
    }
    strategy.init(data);

    double cash = config.initialCapital;
    std::map<std::string, double> positions;  // signed quantities
    std::vector<double> equity(static_cast<std::size_t>(n), 0.0);
    double totalCosts = 0, totalTurnover = 0;

    for (int i = 0; i < n; i++) {
        double portfolioValue = cash + marketValue(positions, data, i);

        if (i % config.rebalanceEveryBars == 0) {
            std::map<std::string, double> weights = strategy.targetWeights(i);
            for (const auto& e : data) {
                const std::string& symbol = e.first;
                auto w = weights.find(symbol);
                double weight = w == weights.end() ? 0.0 : w->second;
                double close = e.second.close(i);
                double targetQty = weight * portfolioValue / close;
                auto pos = positions.find(symbol);
                double currentQty = pos == positions.end() ? 0.0 : pos->second;
                double delta = targetQty - currentQty;
                if (delta == 0) {
                    continue;
                }
                double notional = std::fabs(delta) * close;
                double fee = config.fee(e.second, i, notional);
                cash -= delta * close + fee;
                totalCosts += fee;
                totalTurnover += notional;
                positions[symbol] = targetQty;
            }
        }
        equity[static_cast<std::size_t>(i)] = cash + marketValue(positions, data, i);
    }

    std::map<std::string, double> finalPositions;
    for (const auto& p : positions) {
        if (p.second != 0) {
            finalPositions[p.first] = p.second;
        }
    }
    PerformanceMetrics metrics =
            performance_analytics::compute(equity, {}, config.periodsPerYear);
    return Result{std::move(equity), metrics, totalCosts, totalTurnover,
                  std::move(finalPositions)};
}

}  // namespace portfolio_backtester
}  // namespace quantfinlib
