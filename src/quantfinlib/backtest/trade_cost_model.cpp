#include "quantfinlib/backtest/trade_cost_model.hpp"

#include <optional>
#include <stdexcept>

#include "quantfinlib/microstructure/market_impact_model.hpp"

namespace quantfinlib {
namespace trade_cost_model {

TradeCostModel flat(double bps) {
    if (bps < 0) {
        throw std::invalid_argument("bps must be >= 0");
    }
    return [bps](const BarSeries&, int, double) { return bps; };
}

TradeCostModel institutional(double commissionBps, double halfSpreadBps, double slippageBps,
                             int impactWindow) {
    if (commissionBps < 0 || halfSpreadBps < 0 || slippageBps < 0 || impactWindow < 2) {
        throw std::invalid_argument("cost components must be >= 0 and impactWindow >= 2");
    }
    double flatBps = commissionBps + halfSpreadBps + slippageBps;
    return [flatBps, impactWindow](const BarSeries& series, int index, double notional) {
        if (index < impactWindow) {
            return flatBps;
        }
        std::optional<MarketImpactModel> impact =
                MarketImpactModel::estimate(series, index, impactWindow);
        if (!impact.has_value()) {
            return flatBps;  // no volume data: impact unknowable, flat costs stand
        }
        double shares = notional / series.close(index);
        return flatBps + impact->squareRootImpactBps(shares);
    };
}

}  // namespace trade_cost_model
}  // namespace quantfinlib
