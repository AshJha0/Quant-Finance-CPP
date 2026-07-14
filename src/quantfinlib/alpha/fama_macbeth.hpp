// Port of com.quantfinlib.alpha.FamaMacBeth: Fama-MacBeth cross-sectional
// regression -- per-period regression of forward returns on factor
// exposures, then the factor premium is the time-series mean of the
// period lambdas with a time-series t-stat (cross-sectional correlation
// is absorbed since each period contributes exactly one observation per
// factor). Singular cross-sections are SKIPPED, not fatal.
#pragma once

#include <vector>

namespace quantfinlib {
namespace fama_macbeth {

/**
 * @param premia        mean per-period premium per factor
 * @param tStats        time-series t-stat per factor
 * @param interceptMean mean per-period intercept (should be ~0)
 * @param interceptTStat its t-stat (significant = unexplained returns)
 * @param periodsUsed   cross-sections that had enough assets
 */
struct Result {
    std::vector<double> premia;
    std::vector<double> tStats;
    double interceptMean = 0;
    double interceptTStat = 0;
    int periodsUsed = 0;
};

/**
 * @param exposures      exposures[t][asset][factor] -- the factor loadings KNOWN at t
 * @param forwardReturns forwardReturns[t][asset] -- the return realized AFTER t
 */
Result fit(const std::vector<std::vector<std::vector<double>>>& exposures,
           const std::vector<std::vector<double>>& forwardReturns);

}  // namespace fama_macbeth
}  // namespace quantfinlib
