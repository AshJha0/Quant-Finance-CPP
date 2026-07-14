// Port of com.quantfinlib.backtest.PerformanceMetrics (Java record):
// strategy performance analytics. Returns/drawdown are fractions; ratios
// are annualized.
#pragma once

namespace quantfinlib {

struct PerformanceMetrics {
    double totalReturn = 0;
    double cagr = 0;
    double annualizedReturn = 0;
    double annualizedVolatility = 0;
    double sharpeRatio = 0;
    double sortinoRatio = 0;
    double calmarRatio = 0;
    double maxDrawdown = 0;
    double profitFactor = 0;
    double winRate = 0;
    int tradeCount = 0;
    double finalEquity = 0;
};

}  // namespace quantfinlib
