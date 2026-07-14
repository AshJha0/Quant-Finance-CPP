#include "quantfinlib/backtest/backtester.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

namespace quantfinlib {
namespace backtester {

namespace {

double closePosition(std::vector<Trade>& trades, const BarSeries& series,
                     const BacktestConfig& config, double qty, double entryPrice,
                     double entryCost, int entryIndex, int exitIndex, double fillPrice,
                     const char* reason, double cash) {
    double proceeds = qty * fillPrice;
    double fee = proceeds * config.commissionRate;
    double net = proceeds - fee;
    double pnl = net - entryCost;
    trades.push_back(Trade{series.symbol(), entryIndex, exitIndex,
                           series.timestamp(entryIndex), series.timestamp(exitIndex),
                           entryPrice, fillPrice, qty, pnl,
                           entryCost == 0 ? 0 : pnl / entryCost, reason});
    return cash + net;
}

}  // namespace

BacktestResult run(TradingStrategy& strategy, const BarSeries& series,
                   const BacktestConfig& config) {
    return run(strategy, series, config, 0);
}

BacktestResult run(TradingStrategy& strategy, const BarSeries& series,
                   const BacktestConfig& config, int tradeFrom) {
    int n = series.size();
    if (tradeFrom < 0 || tradeFrom >= n) {
        throw std::invalid_argument("tradeFrom must be in [0, " + std::to_string(n)
                                    + "), got " + std::to_string(tradeFrom));
    }
    strategy.init(series);
    std::vector<double> equity(static_cast<std::size_t>(n - tradeFrom), 0.0);
    std::vector<Trade> trades;

    double stopLoss = strategy.stopLossPct() > 0 ? strategy.stopLossPct() : config.stopLossPct;
    double takeProfit =
            strategy.takeProfitPct() > 0 ? strategy.takeProfitPct() : config.takeProfitPct;

    double cash = config.initialCapital;
    double qty = 0;
    double entryPrice = 0;
    double entryCost = 0;
    int entryIndex = -1;

    for (int i = tradeFrom; i < n; i++) {
        // 1. Intrabar risk exits (only on bars after the entry bar).
        if (qty > 0 && i > entryIndex) {
            if (stopLoss > 0) {
                double stopPrice = entryPrice * (1 - stopLoss);
                if (series.low(i) <= stopPrice) {
                    double fill = std::min(stopPrice, series.open(i));
                    cash = closePosition(trades, series, config, qty, entryPrice, entryCost,
                                         entryIndex, i, fill, Trade::REASON_STOP_LOSS, cash);
                    qty = 0;
                }
            }
            if (qty > 0 && takeProfit > 0) {
                double targetPrice = entryPrice * (1 + takeProfit);
                if (series.high(i) >= targetPrice) {
                    double fill = std::max(targetPrice, series.open(i));
                    cash = closePosition(trades, series, config, qty, entryPrice, entryCost,
                                         entryIndex, i, fill, Trade::REASON_TAKE_PROFIT, cash);
                    qty = 0;
                }
            }
        }

        // 2. Strategy signal at the close.
        Signal sig = strategy.onBar(i);
        double close = series.close(i);
        if (sig == Signal::BUY && qty == 0 && cash > 0) {
            double fill = close * (1 + config.slippageRate);
            double fee = cash * config.commissionRate;
            qty = (cash - fee) / fill;
            entryPrice = fill;
            entryCost = cash;
            entryIndex = i;
            cash = 0;
        } else if (sig == Signal::SELL && qty > 0) {
            double fill = close * (1 - config.slippageRate);
            cash = closePosition(trades, series, config, qty, entryPrice, entryCost,
                                 entryIndex, i, fill, Trade::REASON_SIGNAL, cash);
            qty = 0;
        }

        equity[static_cast<std::size_t>(i - tradeFrom)] = cash + qty * close;
    }

    // 3. Force-close any open position at the final bar.
    if (qty > 0) {
        double fill = series.close(n - 1) * (1 - config.slippageRate);
        cash = closePosition(trades, series, config, qty, entryPrice, entryCost,
                             entryIndex, n - 1, fill, Trade::REASON_END_OF_DATA, cash);
        equity.back() = cash;
    }

    return BacktestResult(strategy.name(), series.symbol(), std::move(equity),
                          std::move(trades), config.periodsPerYear);
}

}  // namespace backtester
}  // namespace quantfinlib
