// Port of com.quantfinlib.markets.PrivateMarketAnalytics (Java):
//   IRR — the money-weighted return: the rate that zeroes the NPV of the
//   fund's cash flows plus terminal NAV. Solved by bisection with an
//   explicit sign-change/bracket check — cash flows that never change
//   sign have no IRR, and this throws rather than inventing one.
//   Multiples — TVPI = (distributions + NAV)/contributions, DPI =
//   distributions/contributions, RVPI = NAV/contributions.
//   Kaplan-Schoar PME — grow every contribution and distribution forward
//   at the INDEX's return and take (FV(distributions) + NAV) /
//   FV(contributions); PME > 1 means the fund beat buying the index with
//   the same cash flows on the same dates.
//   Geltner desmoothing — inverts AR(1) appraisal smoothing
//   r_obs_t = (1-phi) r_true_t + phi r_obs_{t-1}, exactly:
//   r_true_t = (r_obs_t - phi r_obs_{t-1}) / (1 - phi).
// Cash-flow sign convention: contributions (money in) NEGATIVE,
// distributions (money out) POSITIVE. Period-indexed flows; IRR is per
// period.
#pragma once

#include <vector>

namespace quantfinlib {
namespace private_market_analytics {

/**
 * Money-weighted return per period: solves sum cf_t / (1+irr)^t = 0.
 * The final period's cash flow should include terminal NAV as a
 * distribution. cashflows: period-indexed, index 0 = today; must contain
 * at least one negative and one positive flow.
 */
double irr(const std::vector<double>& cashflows);

/** TVPI: total value (distributions + NAV) to paid-in. */
double tvpi(double contributions, double distributions, double nav);

/** DPI: realized distributions to paid-in — the cash-back multiple. */
double dpi(double contributions, double distributions, double nav);

/** RVPI: remaining (appraised) value to paid-in. */
double rvpi(double contributions, double distributions, double nav);

/**
 * Kaplan-Schoar PME. Arrays are period-aligned with `indexLevels` (same
 * length); contributions/distributions are the POSITIVE amounts flowing
 * in each period. Returns PME; > 1 = fund beat the index on its own
 * cash-flow dates.
 */
double ksPme(const std::vector<double>& contributions,
             const std::vector<double>& distributions,
             double terminalNav, const std::vector<double>& indexLevels);

/**
 * Geltner desmoothing: inverts AR(1) appraisal smoothing with parameter
 * `phi` in [0, 1). Element 0 is kept as observed (no lag exists for it).
 */
std::vector<double> geltnerDesmooth(const std::vector<double>& observedReturns,
                                    double phi);

}  // namespace private_market_analytics
}  // namespace quantfinlib
