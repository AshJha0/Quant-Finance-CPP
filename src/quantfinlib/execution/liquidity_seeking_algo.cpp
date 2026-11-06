#include "quantfinlib/execution/liquidity_seeking_algo.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

LiquiditySeekingAlgo::Config::Config(double spreadTolerance_, double maxVolRegime_, double maxImpactBps_,
                                     double maxDepthFraction_, double forceCompleteFrom_)
        : spreadTolerance(spreadTolerance_),
          maxVolRegime(maxVolRegime_),
          maxImpactBps(maxImpactBps_),
          maxDepthFraction(maxDepthFraction_),
          forceCompleteFrom(forceCompleteFrom_) {
    if (spreadTolerance < 0 || maxVolRegime < 0 || maxImpactBps < 0 || maxDepthFraction <= 0
            || maxDepthFraction > 1 || forceCompleteFrom < 0 || forceCompleteFrom >= 1) {
        throw std::invalid_argument("invalid seek config");
    }
}

LiquiditySeekingAlgo::Config LiquiditySeekingAlgo::Config::defaults() {
    return Config(0.10, 0.5, 5.0, 0.25, 0.7);
}

LiquiditySeekingAlgo::LiquiditySeekingAlgo(std::int64_t parentQty, const Config& config)
        : parentQty_(parentQty), config_(config) {
    if (parentQty <= 0) {
        throw std::invalid_argument("parentQty must be > 0");
    }
}

LiquiditySeekingAlgo::LiquiditySeekingAlgo(std::int64_t parentQty)
        : LiquiditySeekingAlgo(parentQty, Config::defaults()) {}

bool LiquiditySeekingAlgo::isCheap(const BenchmarkExecutor::MarketState& m, double forecastSpread) const {
    bool spreadCheap = m.spread >= 0 && forecastSpread > 0
            && forecastSpread != std::numeric_limits<double>::infinity()
            && m.spread <= forecastSpread * (1 + config_.spreadTolerance);
    // NaN fails BOTH remaining gates (NaN <= x is false): an unknowable
    // regime or impact must never authorize a burst.
    bool calm = m.volatility <= config_.maxVolRegime;
    bool lowImpact = m.impactBps <= config_.maxImpactBps;
    return spreadCheap && calm && lowImpact;
}

std::int64_t LiquiditySeekingAlgo::dueQuantity(double scheduleFraction, const BenchmarkExecutor::MarketState& m,
                                               double forecastSpread) {
    std::int64_t remaining = parentQty_ - executed_;
    if (remaining <= 0) {
        return 0;
    }
    if (std::isnan(scheduleFraction)) {
        return 0;  // an unreadable clock releases nothing
    }
    double f = math_utils::clamp(scheduleFraction, 0, 1);

    std::int64_t due = 0;
    if (isCheap(m, forecastSpread)) {
        double depth = m.displayedDepth;
        if (depth == std::numeric_limits<double>::infinity()) {
            due = remaining;
        } else if (depth > 0) {  // NaN depth: no burst
            due = static_cast<std::int64_t>(std::floor(config_.maxDepthFraction * depth));
        }
    }

    // The completion floor: from forceCompleteFrom, the remainder spread
    // linearly over the horizon left; at f = 1 the floor IS the remainder.
    if (f > config_.forceCompleteFrom) {
        double ramp = (f - config_.forceCompleteFrom) / (1 - config_.forceCompleteFrom);
        std::int64_t floor = f >= 1 ? remaining
                                    : static_cast<std::int64_t>(std::ceil(static_cast<double>(remaining) * ramp));
        due = std::max(due, floor);
    }
    return std::min(due, remaining);
}

void LiquiditySeekingAlgo::onFill(std::int64_t qty) {
    if (qty > 0) {
        executed_ += qty;
    }
}

}  // namespace quantfinlib
