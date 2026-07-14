// Port of com.quantfinlib.risk.VarBacktest (Java): VaR model validation.
// Kupiec proportion-of-failures LR (two-sided, chi^2(1)), Christoffersen
// independence LR over exception transitions (chi^2(1)), and the joint
// conditional-coverage test (chi^2(2)) with its exact p-value e^{-LR/2}.
#pragma once

#include <vector>

namespace quantfinlib {
namespace var_backtest {

struct VarBacktestResult {
    int observations;
    int exceptions;
    double expectedExceptions;
    double exceptionRate;
    double kupiecStatistic;
    double kupiecPValue;
    double independenceStatistic;
    double independencePValue;
    double conditionalCoverageStatistic;
    double conditionalCoveragePValue;

    /** Exception frequency consistent with the confidence level? */
    bool calibrated(double significance) const { return kupiecPValue > significance; }

    /** Exceptions arrive independently (no crisis clustering)? */
    bool independent(double significance) const {
        return independencePValue > significance;
    }

    /** Joint test: right rate AND independent. */
    bool passes(double significance) const {
        return conditionalCoveragePValue > significance;
    }
};

/** Backtests a constant VaR (positive loss fraction) against realized returns. */
VarBacktestResult test(const std::vector<double>& returns, double constantVar,
                       double confidence);

/**
 * @param returns      realized periodic returns
 * @param varForecasts VaR forecast for each period as a positive loss
 *                     fraction, aligned with returns
 * @param confidence   the VaR confidence level (e.g. 0.95, 0.99)
 */
VarBacktestResult test(const std::vector<double>& returns,
                       const std::vector<double>& varForecasts, double confidence);

}  // namespace var_backtest
}  // namespace quantfinlib
