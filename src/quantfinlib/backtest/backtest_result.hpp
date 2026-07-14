// Port of com.quantfinlib.backtest.BacktestResult: result of a backtest
// run — full equity curve (one point per bar), completed trade history,
// and derived performance metrics (computed in the constructor exactly
// like the Java class). toString() keeps the Java format string.
#pragma once

#include <string>
#include <vector>

#include "quantfinlib/backtest/performance_metrics.hpp"
#include "quantfinlib/backtest/trade.hpp"

namespace quantfinlib {

class BacktestResult {
public:
    BacktestResult(std::string strategyName, std::string symbol,
                   std::vector<double> equityCurve, std::vector<Trade> trades,
                   int periodsPerYear);

    const std::string& strategyName() const { return strategyName_; }
    const std::string& symbol() const { return symbol_; }
    const std::vector<double>& equityCurve() const { return equityCurve_; }
    const std::vector<Trade>& trades() const { return trades_; }
    const PerformanceMetrics& metrics() const { return metrics_; }

    std::string toString() const;

private:
    std::string strategyName_;
    std::string symbol_;
    std::vector<double> equityCurve_;
    std::vector<Trade> trades_;
    PerformanceMetrics metrics_;
};

}  // namespace quantfinlib
