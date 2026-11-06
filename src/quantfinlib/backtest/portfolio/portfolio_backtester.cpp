#include "quantfinlib/backtest/portfolio/portfolio_backtester.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/backtest/performance_analytics.hpp"
#include "quantfinlib/backtest/volume_cap.hpp"

namespace quantfinlib {
namespace portfolio_backtester {

namespace {

double marketValue(const std::map<std::string, double>& positions,
                   const std::map<std::string, BarSeries>& data, int index, bool atOpen) {
    double value = 0;
    for (const auto& p : positions) {
        const BarSeries& s = data.at(p.first);
        value += p.second * (atOpen ? s.open(index) : s.close(index));
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
    volume_cap::requireRate(config.maxParticipationRate);
    if (config.rebalanceEveryBars <= 0 || config.periodsPerYear <= 0
        || !(config.borrowRateAnnual >= 0) || !(config.financingRateAnnual >= 0)) {
        throw std::invalid_argument("invalid portfolio config");
    }
    std::map<std::string, const Delisting*> delistings;
    for (const Delisting& d : config.delistings) {
        if (data.find(d.symbol) == data.end() || d.barIndex < 1 || d.barIndex >= n
            || !std::isfinite(d.delistingReturn) || d.delistingReturn < -1) {
            throw std::invalid_argument("invalid delisting for " + d.symbol);
        }
        delistings[d.symbol] = &d;
    }
    std::map<std::string, bool> volumeKnown;
    for (const auto& e : data) {
        volumeKnown[e.first] = volume_cap::hasVolumeData(e.second);
    }
    auto isDelisted = [&](const std::string& symbol, int i) {
        auto d = delistings.find(symbol);
        return d != delistings.end() && i >= d->second->barIndex;
    };
    strategy.init(data);

    const bool sameBar = config.fillTiming == FillTiming::SAME_BAR_CLOSE;
    double cash = config.initialCapital;
    std::map<std::string, double> positions;  // signed quantities
    std::vector<double> equity(static_cast<std::size_t>(n), 0.0);
    double totalCosts = 0, totalTurnover = 0, totalBorrow = 0, totalFinancing = 0;

    // Moves positions toward `weights` at bar i's open or close. Trailing
    // cost models read the last bar finished before the fill.
    auto rebalance = [&](const std::map<std::string, double>& weights, int i, bool atOpen) {
        double portfolioValue = cash + marketValue(positions, data, i, atOpen);
        int costIndex = atOpen ? i - 1 : i;
        for (const auto& e : data) {
            const std::string& symbol = e.first;
            if (isDelisted(symbol, i)) {
                continue;
            }
            auto w = weights.find(symbol);
            double weight = w == weights.end() ? 0.0 : w->second;
            double px = atOpen ? e.second.open(i) : e.second.close(i);
            if (!(px > 0) || !std::isfinite(weight)) {
                throw std::invalid_argument("bad price or weight for " + symbol);
            }
            double targetQty = weight * portfolioValue / px;
            auto pos = positions.find(symbol);
            double currentQty = pos == positions.end() ? 0.0 : pos->second;
            double delta = targetQty - currentQty;
            double cap = volume_cap::maxQuantity(e.second, i, config.maxParticipationRate,
                                                 volumeKnown[symbol]);
            delta = std::max(-cap, std::min(cap, delta));
            if (delta == 0) {
                continue;
            }
            double notional = std::fabs(delta) * px;
            double fee = config.fee(e.second, costIndex, notional);
            cash -= delta * px + fee;
            totalCosts += fee;
            totalTurnover += notional;
            positions[symbol] = currentQty + delta;
        }
    };

    bool pending = false;
    std::map<std::string, double> pendingWeights;
    for (int i = 0; i < n; i++) {
        // 1. Instruments that stopped trading pay out at the delisting return.
        for (const auto& d : delistings) {
            auto pos = positions.find(d.first);
            if (d.second->barIndex == i && pos != positions.end()) {
                cash += pos->second * data.at(d.first).close(i - 1)
                        * (1 + d.second->delistingReturn);
                positions.erase(pos);
            }
        }
        // 2. Weights decided on the previous close reach the market.
        if (pending) {
            rebalance(pendingWeights, i, true);
            pending = false;
        }
        // 3. New decision at this close.
        if (i % config.rebalanceEveryBars == 0) {
            std::map<std::string, double> weights = strategy.targetWeights(i);
            if (sameBar) {
                rebalance(weights, i, false);
            } else {
                pendingWeights = std::move(weights);
                pending = true;
            }
        }
        // 4. Carry for holding through this bar.
        double shortValue = 0;
        for (const auto& p : positions) {
            if (p.second < 0) {
                shortValue -= p.second * data.at(p.first).close(i);
            }
        }
        double borrow = shortValue * config.borrowRateAnnual / config.periodsPerYear;
        double financing =
                std::max(0.0, -cash) * config.financingRateAnnual / config.periodsPerYear;
        cash -= borrow + financing;
        totalBorrow += borrow;
        totalFinancing += financing;

        equity[static_cast<std::size_t>(i)] = cash + marketValue(positions, data, i, false);
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
                  std::move(finalPositions), totalBorrow, totalFinancing};
}

}  // namespace portfolio_backtester
}  // namespace quantfinlib
