// Port of com.quantfinlib.backtest.PerformanceAnalytics: computes
// PerformanceMetrics from an equity curve and trade history. The risk
// primitives (annualized vol/Sharpe/Sortino/max drawdown) are 1:1 copies
// of RiskMetrics in risk_helpers.hpp. One added gate versus Java: an
// empty equity curve throws std::invalid_argument instead of raising
// ArrayIndexOutOfBoundsException.
#pragma once

#include <vector>

#include "quantfinlib/backtest/performance_metrics.hpp"
#include "quantfinlib/backtest/trade.hpp"

namespace quantfinlib {
namespace performance_analytics {

PerformanceMetrics compute(const std::vector<double>& equity,
                           const std::vector<Trade>& trades, int periodsPerYear);

}  // namespace performance_analytics
}  // namespace quantfinlib
