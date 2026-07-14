// Port of com.quantfinlib.risk.FrtbEs (Java): FRTB Internal Models
// Approach expected shortfall — ES at 97.5%, the liquidity-horizon
// cascade ES = sqrt(sum_j [ES_j * sqrt((LH_j - LH_{j-1})/10)]^2) per
// MAR33.5, the stressed-calibration multiplier (MAR33.6, ratio FLOORED
// at 1 — a calm stressed period never discounts capital), and the Basel
// backtesting traffic light (GREEN <= 4, AMBER 5-9, RED >= 10 over 250
// days). Styled after BCBS MAR33, not certified.
#pragma once

#include <vector>

namespace quantfinlib {
namespace frtb_es {

/** The five regulatory liquidity horizons, in days. */
inline constexpr int LH_10 = 10;
inline constexpr int LH_20 = 20;
inline constexpr int LH_40 = 40;
inline constexpr int LH_60 = 60;
inline constexpr int LH_120 = 120;

/** ES at 97.5% of a loss sample (positive losses), the FRTB tail measure. */
double es975(const std::vector<double>& losses);

/**
 * The liquidity-horizon cascade (MAR33.5).
 *
 * @param esByHorizon esByHorizon[j] = 10-day ES with only the factors of
 *                    liquidity horizon >= horizons[j] shocked; index 0 is
 *                    the full set at LH 10
 * @param horizons    ascending, starting at 10 (e.g. {10, 20, 60})
 */
double liquidityHorizonEs(const std::vector<double>& esByHorizon,
                          const std::vector<int>& horizons);

/**
 * The stressed-calibration multiplier (MAR33.6): current full-factor ES
 * scaled by the reduced-set stressed/current ratio, FLOORED at 1.
 */
double stressCalibratedEs(double esCurrentFull, double esStressedReduced,
                          double esCurrentReduced);

/**
 * The Basel backtesting traffic light over 250 days of 99% VaR
 * exceptions: GREEN <= 4, AMBER 5-9, RED >= 10.
 */
enum class TrafficLight { GREEN, AMBER, RED };

TrafficLight trafficLightOf(int exceptions250d);

}  // namespace frtb_es
}  // namespace quantfinlib
