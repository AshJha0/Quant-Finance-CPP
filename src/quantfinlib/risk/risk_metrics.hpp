// Port of com.quantfinlib.risk.RiskMetrics (Java): core quantitative
// risk metrics. Return-based metrics take simple periodic returns
// (e.g. daily); VaR/CVaR are reported as positive loss fractions.
// Algorithms copied 1:1 from the Java source.
#pragma once

#include <vector>

namespace quantfinlib {
namespace risk_metrics {

inline constexpr int TRADING_DAYS_PER_YEAR = 252;

/** Per-period sample volatility. */
double volatility(const std::vector<double>& returns);

double annualizedVolatility(const std::vector<double>& returns, int periodsPerYear);

/**
 * Historical Value at Risk at the given confidence level (e.g. 0.95).
 * Returned as a positive loss fraction; 0 if the quantile is a gain.
 */
double historicalVar(const std::vector<double>& returns, double confidence);

/** Parametric (Gaussian) VaR at the given confidence level. */
double parametricVar(const std::vector<double>& returns, double confidence);

/** Conditional VaR / Expected Shortfall: mean loss beyond the VaR threshold. */
double conditionalVar(const std::vector<double>& returns, double confidence);

/** Alias for conditionalVar. */
double expectedShortfall(const std::vector<double>& returns, double confidence);

/** Annualized Sharpe ratio; riskFreeRate is annual. */
double sharpeRatio(const std::vector<double>& returns, double riskFreeRate,
                   int periodsPerYear);

/** Annualized Sortino ratio using downside deviation below the periodic MAR. */
double sortinoRatio(const std::vector<double>& returns, double riskFreeRate,
                    int periodsPerYear);

/** Per-period downside deviation below the minimum acceptable return. */
double downsideDeviation(const std::vector<double>& returns, double mar);

/** Maximum peak-to-trough drawdown of an equity curve, as a positive fraction. */
double maxDrawdown(const std::vector<double>& equity);

/** Beta of an asset versus a benchmark (equal-length return series). */
double beta(const std::vector<double>& assetReturns,
            const std::vector<double>& benchmarkReturns);

double correlation(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace risk_metrics
}  // namespace quantfinlib
