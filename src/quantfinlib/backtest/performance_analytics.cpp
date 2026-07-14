#include "quantfinlib/backtest/performance_analytics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/backtest/risk_helpers.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace performance_analytics {

PerformanceMetrics compute(const std::vector<double>& equity,
                           const std::vector<Trade>& trades, int periodsPerYear) {
    int n = static_cast<int>(equity.size());
    if (n == 0) {
        // Java indexes equity[0] unguarded (AIOOBE); gate explicitly here.
        throw std::invalid_argument("equity curve must not be empty");
    }
    double start = equity[0];
    double end = equity[n - 1];
    double totalReturn = end / start - 1;

    double cagr = 0;
    if (n > 1 && end > 0 && start > 0) {
        cagr = std::pow(end / start, static_cast<double>(periodsPerYear) / (n - 1)) - 1;
    }

    std::vector<double> rets(static_cast<std::size_t>(std::max(0, n - 1)));
    for (int i = 1; i < n; i++) {
        rets[i - 1] = equity[i] / equity[i - 1] - 1;
    }

    double annReturn = rets.empty() ? 0 : math_utils::mean(rets) * periodsPerYear;
    double annVol = rets.empty() ? 0
            : backtest_detail::annualizedVolatility(rets, periodsPerYear);
    double sharpe = rets.empty() ? 0
            : backtest_detail::sharpeRatio(rets, 0, periodsPerYear);
    double sortino = rets.empty() ? 0
            : backtest_detail::sortinoRatio(rets, 0, periodsPerYear);
    double maxDd = backtest_detail::maxDrawdown(equity);
    double calmar = maxDd == 0 ? 0 : cagr / maxDd;

    double grossProfit = 0, grossLoss = 0;
    int wins = 0;
    for (const Trade& t : trades) {
        if (t.pnl >= 0) {
            grossProfit += t.pnl;
            if (t.pnl > 0) {
                wins++;
            }
        } else {
            grossLoss -= t.pnl;
        }
    }
    double profitFactor = grossLoss == 0
            ? (grossProfit > 0 ? std::numeric_limits<double>::infinity() : 0)
            : grossProfit / grossLoss;
    double winRate = trades.empty() ? 0
            : static_cast<double>(wins) / static_cast<double>(trades.size());

    PerformanceMetrics m;
    m.totalReturn = totalReturn;
    m.cagr = cagr;
    m.annualizedReturn = annReturn;
    m.annualizedVolatility = annVol;
    m.sharpeRatio = sharpe;
    m.sortinoRatio = sortino;
    m.calmarRatio = calmar;
    m.maxDrawdown = maxDd;
    m.profitFactor = profitFactor;
    m.winRate = winRate;
    m.tradeCount = static_cast<int>(trades.size());
    m.finalEquity = end;
    return m;
}

}  // namespace performance_analytics
}  // namespace quantfinlib
