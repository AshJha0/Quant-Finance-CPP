#include "quantfinlib/backtest/backtester.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantfinlib/backtest/volume_cap.hpp"

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
    volume_cap::requireRate(config.maxParticipationRate);
    strategy.init(series);
    std::vector<double> equity(static_cast<std::size_t>(n - tradeFrom), 0.0);
    std::vector<Trade> trades;

    double stopLoss = strategy.stopLossPct() > 0 ? strategy.stopLossPct() : config.stopLossPct;
    double takeProfit =
            strategy.takeProfitPct() > 0 ? strategy.takeProfitPct() : config.takeProfitPct;

    const bool sameBar = config.fillTiming == FillTiming::SAME_BAR_CLOSE;
    const bool volumeKnown = volume_cap::hasVolumeData(series);

    double cash = config.initialCapital;
    double qty = 0;
    double entryPrice = 0;
    double entryCost = 0;
    int entryIndex = -1;
    bool buyPending = false;
    const char* exitPending = nullptr;  // reason of an exit still to be worked

    auto enter = [&](int i, double fill) {
        double fee = cash * config.commissionRate;
        double wanted = (cash - fee) / fill;
        double bought = std::min(
                wanted, volume_cap::maxQuantity(series, i, config.maxParticipationRate, volumeKnown));
        if (!(bought > 0)) {
            return;
        }
        // All-in cash for `bought`: the whole balance unless the cap bit.
        double spend = bought < wanted ? cash * (bought / wanted) : cash;
        qty = bought;
        entryPrice = fill;
        entryCost = spend;
        entryIndex = i;
        cash -= spend;
    };
    auto sell = [&](int i, double fill, const char* reason) {
        double sold = std::min(
                qty, volume_cap::maxQuantity(series, i, config.maxParticipationRate, volumeKnown));
        exitPending = reason;
        if (!(sold > 0)) {
            return;
        }
        double costShare = sold < qty ? entryCost * (sold / qty) : entryCost;
        cash = closePosition(trades, series, config, sold, entryPrice, costShare, entryIndex, i,
                             fill, reason, cash);
        entryCost -= costShare;
        qty = sold < qty ? qty - sold : 0;
        if (qty == 0) {
            exitPending = nullptr;
        }
    };

    for (int i = tradeFrom; i < n; i++) {
        // 0. Orders decided on an earlier bar reach the market at this open.
        if (exitPending != nullptr) {
            sell(i, series.open(i) * (1 - config.slippageRate), exitPending);
        } else if (buyPending) {
            enter(i, series.open(i) * (1 + config.slippageRate));
        }
        buyPending = false;

        // 1. Intrabar risk exits. An entry at this bar's open is exposed to
        //    the rest of the bar; an entry at a close is not, and cannot be
        //    open yet at this step.
        if (qty > 0 && exitPending == nullptr) {
            if (stopLoss > 0) {
                double stopPrice = entryPrice * (1 - stopLoss);
                if (series.low(i) <= stopPrice) {
                    double fill = std::min(stopPrice, series.open(i));
                    sell(i, fill * (1 - config.slippageRate), Trade::REASON_STOP_LOSS);
                }
            }
            if (qty > 0 && exitPending == nullptr && takeProfit > 0) {
                double targetPrice = entryPrice * (1 + takeProfit);
                if (series.high(i) >= targetPrice) {
                    double fill = std::max(targetPrice, series.open(i));
                    sell(i, fill, Trade::REASON_TAKE_PROFIT);
                }
            }
        }

        // 2. Strategy signal at the close.
        Signal sig = strategy.onBar(i);
        double close = series.close(i);
        if (sig == Signal::BUY && qty == 0 && cash > 0) {
            if (sameBar) {
                double fill = close * (1 + config.slippageRate);
                enter(i, fill);
            } else {
                buyPending = true;
            }
        } else if (sig == Signal::SELL && qty > 0 && exitPending == nullptr) {
            if (sameBar) {
                sell(i, close * (1 - config.slippageRate), Trade::REASON_SIGNAL);
            } else {
                exitPending = Trade::REASON_SIGNAL;
            }
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
