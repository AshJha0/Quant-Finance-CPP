// Port of com.quantfinlib.pricing.VarianceSwap (Java): VARIANCE SWAP
// analytics. The fair strike is MODEL-FREE (Demeterfi-Derman-Kamal-Zou
// 1999): the 1/K^2 option portfolio replicates the log contract, so
// fairVariance() delegates to volatility::VolatilityIndex and squares it
// — a VIX of 20 IS a variance-swap strike of 0.04. Also here: the
// Brockhaus-Long vol-swap convexity correction, the vega-to-variance
// notional bridge (varianceNotional = vegaNotional / (2 * strikeVol)),
// and the additive-in-time mark-to-market of a seasoned swap. Variance
// in annualized decimal^2 (0.04 = 20 vol), time in years.
#pragma once

#include <vector>

namespace quantfinlib {
namespace variance_swap {

/**
 * Model-free fair strike (annualized VARIANCE, e.g. 0.04) replicated from
 * one expiry's option chain — parameters exactly as VolatilityIndex::index.
 */
double fairVariance(const std::vector<double>& strikes,
                    const std::vector<double>& putMids,
                    const std::vector<double>& callMids,
                    double forward, double rate, double tYears);

/**
 * VOLATILITY swap fair strike via the Brockhaus-Long convexity correction:
 * E[sqrt(V)] ~ sqrt(E[V]) - Var(V) / (8 * E[V]^1.5). The vol strike is
 * always BELOW the square root of the variance strike (Jensen).
 */
double volSwapStrike(double fairVariance, double varianceOfVariance);

/**
 * Variance notional from a vega-notional quote: vegaNotional / (2 * strikeVol).
 * strikeVol is the strike in VOL terms (0.20, not 0.04).
 */
double varianceNotional(double vegaNotional, double strikeVol);

/**
 * Mark-to-market of a seasoned variance swap per unit of variance notional:
 * e^{-r(T-t)} * [ (t/T) * realized + ((T-t)/T) * K_rem - K_0 ].
 */
double markToMarket(double strikeVariance, double realizedVariance,
                    double remainingFair, double tElapsedYears,
                    double tTotalYears, double rate);

}  // namespace variance_swap
}  // namespace quantfinlib
