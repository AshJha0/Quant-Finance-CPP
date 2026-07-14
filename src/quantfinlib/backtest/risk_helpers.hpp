// INTERNAL helpers: the handful of com.quantfinlib.risk.RiskMetrics
// functions the backtest analytics call (sharpe, sortino, max drawdown,
// beta). The risk domain itself is not ported yet; these are 1:1 copies
// of the Java bodies so PerformanceAnalytics / BenchmarkComparison /
// BlockBootstrap keep identical numerics. When RiskMetrics is ported,
// this header should delegate to it.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace backtest_detail {

/** Per-period sample volatility (RiskMetrics.volatility). */
inline double volatility(const std::vector<double>& returns) {
    return math_utils::stdDev(returns);
}

inline double annualizedVolatility(const std::vector<double>& returns, int periodsPerYear) {
    return volatility(returns) * std::sqrt(static_cast<double>(periodsPerYear));
}

/** Annualized Sharpe ratio; 0 when volatility is 0 (RiskMetrics.sharpeRatio). */
inline double sharpeRatio(const std::vector<double>& returns, double riskFreeRate,
                          int periodsPerYear) {
    double vol = annualizedVolatility(returns, periodsPerYear);
    if (vol == 0) {
        return 0;
    }
    double annualReturn = math_utils::mean(returns) * periodsPerYear;
    return (annualReturn - riskFreeRate) / vol;
}

/** Per-period downside deviation below the MAR (RiskMetrics.downsideDeviation). */
inline double downsideDeviation(const std::vector<double>& returns, double mar) {
    double s = 0;
    for (double r : returns) {
        double d = std::min(0.0, r - mar);
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(returns.size()));
}

/** Annualized Sortino ratio; 0 when downside deviation is 0. */
inline double sortinoRatio(const std::vector<double>& returns, double riskFreeRate,
                           int periodsPerYear) {
    double mar = riskFreeRate / periodsPerYear;
    double dd = downsideDeviation(returns, mar) * std::sqrt(static_cast<double>(periodsPerYear));
    if (dd == 0) {
        return 0;
    }
    double annualReturn = math_utils::mean(returns) * periodsPerYear;
    return (annualReturn - riskFreeRate) / dd;
}

/** Max peak-to-trough drawdown of an equity curve, positive fraction. */
inline double maxDrawdown(const std::vector<double>& equity) {
    double peak = equity[0];
    double maxDd = 0;
    for (double e : equity) {
        peak = std::max(peak, e);
        if (peak > 0) {
            maxDd = std::max(maxDd, (peak - e) / peak);
        }
    }
    return maxDd;
}

/** Beta of an asset versus a benchmark; 0 when the benchmark is flat. */
inline double beta(const std::vector<double>& assetReturns,
                   const std::vector<double>& benchmarkReturns) {
    double varB = math_utils::variance(benchmarkReturns);
    if (varB == 0) {
        return 0;
    }
    return math_utils::covariance(assetReturns, benchmarkReturns) / varB;
}

}  // namespace backtest_detail
}  // namespace quantfinlib
