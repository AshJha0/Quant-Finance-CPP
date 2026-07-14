#include "quantfinlib/backtest/backtest_result.hpp"

#include <cstdio>

#include "quantfinlib/backtest/performance_analytics.hpp"

namespace quantfinlib {

BacktestResult::BacktestResult(std::string strategyName, std::string symbol,
                               std::vector<double> equityCurve, std::vector<Trade> trades,
                               int periodsPerYear)
        : strategyName_(std::move(strategyName)),
          symbol_(std::move(symbol)),
          equityCurve_(std::move(equityCurve)),
          trades_(std::move(trades)),
          metrics_(performance_analytics::compute(equityCurve_, trades_, periodsPerYear)) {}

std::string BacktestResult::toString() const {
    const PerformanceMetrics& m = metrics_;
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "%s on %s: totalReturn=%.2f%%, CAGR=%.2f%%, sharpe=%.2f, sortino=%.2f, "
                  "calmar=%.2f, maxDD=%.2f%%, profitFactor=%.2f, winRate=%.1f%%, trades=%d",
                  strategyName_.c_str(), symbol_.c_str(), m.totalReturn * 100, m.cagr * 100,
                  m.sharpeRatio, m.sortinoRatio, m.calmarRatio, m.maxDrawdown * 100,
                  m.profitFactor, m.winRate * 100, m.tradeCount);
    return std::string(buf);
}

}  // namespace quantfinlib
