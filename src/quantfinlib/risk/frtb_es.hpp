// Port of com.quantfinlib.risk.FrtbEs (Java): FRTB Internal Models
// Approach expected shortfall — ES at 97.5%, the liquidity-horizon
// cascade ES = sqrt(sum_j [ES_j * sqrt((LH_j - LH_{j-1})/10)]^2) per
// MAR33.4, the stressed calibration ES = ES_R,S * max(1, ES_F,C / ES_R,C)
// (MAR33.5-33.6), and the Basel
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

/**
 * ES at 97.5% of a loss sample (positive losses), the FRTB tail measure, by
 * the Acerbi-Tasche estimator of tail_risk, floored at zero. On 250 days the
 * tail holds 6.25 observations: the worst six plus a quarter of the seventh.
 */
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

/** Minimum share of full-set current ES the reduced set must explain (MAR33.5(2)). */
inline constexpr double MIN_REDUCED_SET_COVERAGE = 0.75;

/**
 * Stressed calibration: ES = ES_R,S * max(1, ES_F,C / ES_R,C). The stressed
 * reduced-set ES is the base; the full/reduced ratio measured today scales it
 * up for factors the stress period does not cover. Example: ES_F,C = 100,
 * ES_R,C = 80, ES_R,S = 150 gives 187.5. Throws on negative or non-finite
 * input and when ES_R,C < 0.75 * ES_F,C.
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
