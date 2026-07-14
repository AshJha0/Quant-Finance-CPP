// Port of com.quantfinlib.markets.IndexConstruction (Java): the
// arithmetic behind "the market was up 1%". Three weighting schemes:
// cap-weighted (weight ∝ price * shares * float — self-rebalancing),
// price-weighted (the Dow's accident), equal-weighted (1/N, trades every
// rebalance). The DIVISOR is how a level series survives membership and
// share changes: level = sum(price * shares * float) / divisor, and
// newDivisor = oldDivisor * newAggregate / oldAggregate keeps the level
// CONTINUOUS through the change. turnover(w1, w2) = 0.5 * sum |w1 - w2|
// is the one-way fraction of the portfolio that must trade.
#pragma once

#include <vector>

namespace quantfinlib {
namespace index_construction {

/** Float-adjusted cap weights: w_i ∝ price_i * shares_i * float_i. */
std::vector<double> capWeights(const std::vector<double>& prices,
                               const std::vector<double>& shares,
                               const std::vector<double>& floatFactors);

/** Price weights: w_i ∝ price_i (the Dow's accident). */
std::vector<double> priceWeights(const std::vector<double>& prices);

/** Equal weights, 1/N. */
std::vector<double> equalWeights(int n);

/** Index level from an aggregate and a divisor. */
double level(const std::vector<double>& prices, const std::vector<double>& shares,
             const std::vector<double>& floatFactors, double divisor);

/**
 * The rescaled divisor that keeps the level CONTINUOUS through a
 * membership/share/float change: pass the aggregate cap before and after
 * the change (both at the same instant).
 */
double adjustDivisor(double oldDivisor, double oldAggregate, double newAggregate);

/** One-way turnover between two aligned weight vectors: 0.5 * sum|w1-w2|. */
double turnover(const std::vector<double>& from, const std::vector<double>& to);

}  // namespace index_construction
}  // namespace quantfinlib
