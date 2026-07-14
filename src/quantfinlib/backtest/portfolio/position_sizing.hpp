// Port of com.quantfinlib.backtest.portfolio.PositionSizing: Kelly,
// fixed-fractional risk, inverse-volatility weighting, and volatility
// targeting. kellyFraction is the full Kelly f* = mu / sigma^2 (0 when
// variance is 0); fixedFractionalQuantity throws std::invalid_argument
// when entry equals stop (undefined risk); inverseVolatilityWeights
// falls back to equal weight if any vol is <= 0.
#pragma once

#include <vector>

namespace quantfinlib {
namespace position_sizing {

/** Full Kelly fraction for a return stream: f* = mu / sigma^2. */
double kellyFraction(double meanReturn, double variance);

/** Half-Kelly — the practitioner's standard, trading growth for drawdown. */
double halfKelly(double meanReturn, double variance);

/**
 * Fixed-fractional sizing: shares such that hitting the stop loses exactly
 * riskFraction of equity.
 */
double fixedFractionalQuantity(double equity, double riskFraction,
                               double entryPrice, double stopPrice);

/** Normalized inverse-volatility weights (equal weight for any zero vols). */
std::vector<double> inverseVolatilityWeights(const std::vector<double>& vols);

/** Leverage multiplier that scales current volatility to the target. */
double volatilityTargetLeverage(double currentAnnualVol, double targetAnnualVol);

}  // namespace position_sizing
}  // namespace quantfinlib
