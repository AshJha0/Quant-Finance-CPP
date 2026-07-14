// Port of com.quantfinlib.microstructure.LiquidityMeasures: liquidity
// estimated from bars alone -- Roll (1984) implied effective spread from
// bid-ask bounce (NaN, not zero, when the autocovariance is non-negative
// -- the bounce signature is absent), Corwin-Schultz (2012) high-low
// range spread (clamped to >= 0), and Amihud (2002) illiquidity.
#pragma once

#include <vector>

namespace quantfinlib {
namespace liquidity_measures {

/** Roll's implied effective spread; NaN when the bounce signature is absent. */
double rollSpread(const std::vector<double>& prices);

/** Corwin-Schultz high-low spread estimate as a fraction of price; clamped to >= 0. */
double corwinSchultzSpread(double high1, double low1, double high2, double low2);

/** Amihud illiquidity: mean(|return| / dollarVolume). Zero volume throws. */
double amihudIlliquidity(const std::vector<double>& returns, const std::vector<double>& dollarVolumes);

}  // namespace liquidity_measures
}  // namespace quantfinlib
