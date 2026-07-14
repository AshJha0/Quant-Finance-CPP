// Port of com.quantfinlib.alpha.PortfolioConstruction: turns raw factor
// scores into tradeable weight vectors via a chain of small, composable,
// pure functions. All functions take and return weight arrays aligned
// with AlphaContext::symbols(); NaN scores become zero weight.
#pragma once

#include <limits>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/alpha/alpha_context.hpp"

namespace quantfinlib {
namespace portfolio_construction {

/**
 * Sentinel prefix marking a symbol with no known sector in
 * sectorNeutralize(ctx, weights, sectorBySymbol): each such symbol gets a
 * unique per-symbol label (prefix + symbol), so it demeans against itself
 * (to zero) instead of polluting a real sector's offset. Java used a
 * NUL-prefixed string for this; this port uses a documented, printable
 * sentinel constant instead (behaviorally identical: collision-proof
 * against any real sector label).
 */
extern const std::string UNKNOWN_SECTOR_PREFIX;

/** zScoreWeights without a per-name cap. */
std::vector<double> zScoreWeights(const std::vector<double>& scores, double grossTarget);

/**
 * Z-score sizing: demean scores cross-sectionally, scale by dispersion,
 * clamp at +-3 sigma, normalize to sum(|w|) = grossTarget, cap per-name at maxWeight.
 */
std::vector<double> zScoreWeights(const std::vector<double>& scores, double grossTarget,
                                   double maxWeight);

/** Caps each |weight| at maxWeight, then re-normalizes toward grossTarget without breaching it. */
std::vector<double> capWeights(const std::vector<double>& weights, double maxWeight,
                                double grossTarget);

/** Inverse-volatility risk budgeting; throws on an unusable (NaN/<=0) vol for an active position. */
std::vector<double> inverseVolBudget(const std::vector<double>& weights,
                                      const std::vector<double>& vols, double grossTarget);

/** Trailing return volatilities per symbol at index (the standard input to inverseVolBudget). */
std::vector<double> trailingVols(const AlphaContext& ctx, int index, int lookback);

/** sectorNeutralize with alignment by construction (resolves against ctx's frozen symbol order). */
std::vector<double> sectorNeutralize(const AlphaContext& ctx, const std::vector<double>& weights,
                                      const std::unordered_map<std::string, std::string>& sectorBySymbol);

/** Sector neutrality: demeans weights within each sector. */
std::vector<double> sectorNeutralize(const std::vector<double>& weights,
                                      const std::vector<std::string>& sectors);

/** Beta neutrality: projects the weight vector orthogonal to the beta vector. */
std::vector<double> betaNeutralize(const std::vector<double>& weights, const std::vector<double>& betas);

/** Trailing OLS betas of each symbol against the equal-weight universe return. */
std::vector<double> trailingBetas(const AlphaContext& ctx, int index, int lookback);

/** Unconstrained mean-variance tilt: w proportional to Sigma^-1 * alpha. */
std::vector<double> meanVarianceTilt(const std::vector<double>& alphas,
                                      const std::vector<std::vector<double>>& covariance,
                                      double grossTarget);

/** Scales weights in place so sum(|w|) = grossTarget (no-op on a flat book). */
void normalizeGross(std::vector<double>& w, double grossTarget);

double gross(const std::vector<double>& w);

}  // namespace portfolio_construction
}  // namespace quantfinlib
