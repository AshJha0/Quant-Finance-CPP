// Port of com.quantfinlib.alpha.AlphaReport: alpha reporting diagnostics --
// alpha decay (IC per horizon + half-life), OLS factor attribution,
// equity-curve helpers (returns, drawdown, rolling Sharpe), and the full
// ratio set via PerformanceAnalytics.
#pragma once

#include <string>
#include <vector>

#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"
#include "quantfinlib/backtest/performance_metrics.hpp"

namespace quantfinlib {
namespace alpha_report {

/** IC per horizon plus the interpolated half-life of the shortest-horizon IC. */
struct Decay {
    std::vector<int> horizons;
    std::vector<double> meanIcs;
    double halfLifeBars = 0;

    std::string format() const;
};

/**
 * Evaluates the factor's mean IC at each horizon (ascending, >= 2). The
 * half-life is the first horizon (linearly interpolated) where the IC
 * falls below half of its shortest-horizon value; +inf when it never
 * does within the tested range.
 */
Decay decayProfile(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex,
                    const std::vector<int>& horizons);

/** OLS attribution: per-bar residual alpha, factor betas, and fit quality. */
struct Attribution {
    double alphaPerBar = 0;
    std::vector<double> betas;
    std::vector<std::string> factorNames;
    double rSquared = 0;

    std::string format() const;
};

/**
 * Regresses portfolio returns on factor return streams (with an
 * intercept) via the normal equations. Throws on any NaN bar (trim
 * warm-up before attributing).
 */
Attribution attribute(const std::vector<double>& portfolioReturns,
                       const std::vector<std::vector<double>>& factorReturns,
                       const std::vector<std::string>& factorNames);

/** Per-bar simple returns of an equity curve. */
std::vector<double> returnsOf(const std::vector<double>& equity);

/** Drawdown series: fraction below the running peak (0 at new highs). */
std::vector<double> drawdownCurve(const std::vector<double>& equity);

/** Rolling annualized Sharpe over a trailing window of per-bar returns; NaN until the window fills. */
std::vector<double> rollingSharpe(const std::vector<double>& returns, int window, int periodsPerYear);

/** The full ratio set on an equity curve, via PerformanceAnalytics. */
PerformanceMetrics summarize(const std::vector<double>& equity, int periodsPerYear);

}  // namespace alpha_report
}  // namespace quantfinlib
