// Port of com.quantfinlib.backtest.validation.BlockBootstrap: stationary
// block bootstrap (Politis-Romano) — blocks of geometric mean length L
// restart with probability 1/L and wrap circularly, preserving the local
// dependence an iid resample destroys. sharpeSamples returns the sorted
// bootstrap distribution of ANNUALIZED Sharpe. DEVIATION: Java draws from
// java.util.Random; here the shared Java-exact SplittableRandom supplies
// the stream instead (one RNG for the whole port). Still deterministic
// per seed; the Java tests only pin properties of the distribution, and
// those pins are ported unchanged. Gates map to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"

namespace quantfinlib {
namespace block_bootstrap {

/**
 * Bootstrap distribution of annualized Sharpe, sorted ascending — read
 * percentiles with math_utils::percentileSorted.
 *
 * @param returns         per-period strategy returns, >= 50 finite
 * @param meanBlockLength geometric mean block length L, >= 1 (1 = iid)
 * @param resamples       bootstrap paths, >= 100
 * @param periodsPerYear  annualization (252 for daily)
 * @param seed            deterministic seed
 */
std::vector<double> sharpeSamples(const std::vector<double>& returns, int meanBlockLength,
                                  int resamples, int periodsPerYear, std::int64_t seed);

/** One stationary-bootstrap path (circular, geometric blocks). */
std::vector<double> resample(const std::vector<double>& series, int meanBlockLength,
                             SplittableRandom& rnd);

}  // namespace block_bootstrap
}  // namespace quantfinlib
