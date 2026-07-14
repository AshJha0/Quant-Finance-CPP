// Port of com.quantfinlib.backtest.portfolio.PortfolioStrategy (Java
// interface): a multi-asset, weight-based strategy for the
// PortfolioBacktester. All series must be index-aligned (same length,
// same bar times). Weights are fractions of current equity: positive =
// long, negative = short, missing symbol = flat; |weights| may sum
// above 1 for leverage. The Java Map<String, BarSeries> becomes
// std::map<std::string, BarSeries> (sorted keys — deterministic
// iteration like the engine's sorted symbol list).
#pragma once

#include <map>
#include <string>

#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {

class PortfolioStrategy {
public:
    virtual ~PortfolioStrategy() = default;

    virtual std::string name() const = 0;

    virtual void init(const std::map<std::string, BarSeries>& data) = 0;

    /** Target weights by symbol as of bar `index` (decided at that bar's close). */
    virtual std::map<std::string, double> targetWeights(int index) = 0;
};

}  // namespace quantfinlib
