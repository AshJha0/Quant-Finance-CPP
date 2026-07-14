// Port of com.quantfinlib.backtest.validation.OverfitProbability:
// probability of backtest overfitting via combinatorially symmetric
// cross-validation — CSCV (Bailey, Borwein, Lopez de Prado & Zhu 2015).
// Slice the T x N return matrix into S blocks, enumerate all C(S, S/2)
// symmetric IS/OOS splits, pick the IS winner, rank it OOS, record the
// logit; PBO = fraction of splits with logit <= 0. Trailing periods that
// don't fill a block are dropped; S is capped at 16 (C(16,8) = 12,870).
// Deterministic (no RNG). Java's ToDoubleFunction objective becomes
// std::function; IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <functional>
#include <vector>

namespace quantfinlib {
namespace overfit_probability {

struct Result {
    double pbo = 0;             // fraction of splits whose IS winner ranked <= OOS median
    int combinations = 0;       // number of symmetric splits, C(S, S/2)
    std::vector<double> logits; // one lambda per combination (enumeration order)
};

/**
 * CSCV with the caller's objective (applied to a concatenated return
 * sub-series; higher is better, must return a finite value).
 *
 * @param returns   T x N rectangular matrix: returns[t][j] = period-t
 *                  return of strategy variant j; all finite, N >= 2
 * @param blocks    S: even, 4 <= S <= 16; each block needs >= 2 periods
 */
Result cscv(const std::vector<std::vector<double>>& returns, int blocks,
            const std::function<double(const std::vector<double>&)>& objective);

/**
 * CSCV with the per-period Sharpe objective mean / stdDev (sample); a
 * zero-variance sub-series scores 0.
 */
Result cscvSharpe(const std::vector<std::vector<double>>& returns, int blocks);

}  // namespace overfit_probability
}  // namespace quantfinlib
