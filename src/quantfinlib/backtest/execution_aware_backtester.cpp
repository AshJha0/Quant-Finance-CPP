#include "quantfinlib/backtest/execution_aware_backtester.hpp"

#include <algorithm>

namespace quantfinlib {

ExecutionAwareBacktester::ExecutionAwareBacktester(TradingStrategy& strategy,
                                                   const BarSeries& series,
                                                   const BacktestConfig& config,
                                                   ExecutionModel& model)
        : strategy_(strategy),
          series_(series),
          config_(config),
          model_(model),
          stopLossPct_(strategy.stopLossPct() > 0 ? strategy.stopLossPct() : config.stopLossPct),
          takeProfitPct_(strategy.takeProfitPct() > 0 ? strategy.takeProfitPct()
                                                      : config.takeProfitPct) {}

ExecutionAwareResult ExecutionAwareBacktester::run(TradingStrategy& strategy,
                                                   const BarSeries& series,
                                                   const BacktestConfig& config,
                                                   ExecutionModel& model) {
    return ExecutionAwareBacktester(strategy, series, config, model).execute();
}

ExecutionAwareResult ExecutionAwareBacktester::execute() {
    strategy_.init(series_);
    int n = series_.size();
    std::vector<double> equity(static_cast<std::size_t>(n), 0.0);
    cash_ = config_.initialCapital;

    for (int i = 0; i < n; i++) {
        // 1. Keep working whatever parent order is open.
        if (pendingExit_ > 0) {
            workExit(i);
        } else if (pendingEntry_ > 0) {
            workEntry(i);
        }
        // 2. Intrabar risk exits on the accumulated position.
        checkRiskExits(i);
        // 3. Strategy signal at the close.
        onSignal(strategy_.onBar(i), i);

        equity[static_cast<std::size_t>(i)] =
                cash_ + static_cast<double>(position_) * series_.close(i);
    }
    forceClose(n - 1, equity);

    std::vector<ParentOrder> parentRecords;
    parentRecords.reserve(parents_.size());
    for (const ParentState& p : parents_) {
        parentRecords.push_back(p.toRecord());
    }
    BacktestResult result(strategy_.name(), series_.symbol(), std::move(equity), trades_,
                          config_.periodsPerYear);
    return ExecutionAwareResult(std::move(result), std::move(parentRecords), series_);
}

// ---------------------------------------------------------------------

void ExecutionAwareBacktester::workEntry(int i) {
    // Cap the request by what cash can actually pay for, priced at the
    // model's own fill anchor (close for most models, the OPEN for
    // last-look) with its declared worst-case all-in cost on top — a
    // flat close-based 1% buffer overdraws cash the moment a model
    // charges more, or fills off a gapped open.
    double ref = model_.referencePrice(series_, i);
    std::int64_t affordable =
            static_cast<std::int64_t>(cash_ / (ref * (1 + model_.worstCaseCostFraction())));
    std::int64_t request = std::min(pendingEntry_, affordable);
    if (request <= 0) {
        return;
    }
    ParentState& entry = parents_[static_cast<std::size_t>(entry_)];
    for (const Execution& f : model_.execute(Side::BUY, request, series_, i)) {
        double cost = f.notional();
        cash_ -= cost;
        entryCost_ += cost;
        position_ += f.quantity;
        totalEntryQty_ += f.quantity;
        pendingEntry_ -= f.quantity;
        entry.fills.push_back(f);
        entry.bars.push_back(i);
        if (firstEntryBar_ < 0) {
            firstEntryBar_ = i;
        }
    }
}

void ExecutionAwareBacktester::workExit(int i) {
    ParentState& exit = parents_[static_cast<std::size_t>(exit_)];
    for (const Execution& f : model_.execute(Side::SELL, pendingExit_, series_, i)) {
        double proceeds = f.notional();
        cash_ += proceeds;
        exitProceeds_ += proceeds;
        position_ -= f.quantity;
        pendingExit_ -= f.quantity;
        exit.fills.push_back(f);
        exit.bars.push_back(i);
    }
    if (position_ == 0 && pendingExit_ == 0) {
        closeRoundTrip(i);
    }
}

void ExecutionAwareBacktester::checkRiskExits(int i) {
    if (position_ <= 0 || pendingExit_ > 0 || firstEntryBar_ < 0 || i <= firstEntryBar_) {
        return;
    }
    double avgEntry = entryCost_ / static_cast<double>(totalEntryQty_);
    const char* reason = nullptr;
    if (stopLossPct_ > 0 && series_.low(i) <= avgEntry * (1 - stopLossPct_)) {
        reason = Trade::REASON_STOP_LOSS;
    } else if (takeProfitPct_ > 0 && series_.high(i) >= avgEntry * (1 + takeProfitPct_)) {
        reason = Trade::REASON_TAKE_PROFIT;
    }
    if (reason != nullptr) {
        startExit(i, reason);
        workExit(i);
    }
}

void ExecutionAwareBacktester::onSignal(Signal signal, int i) {
    if (signal == Signal::BUY && position_ == 0 && pendingEntry_ == 0 && pendingExit_ == 0) {
        double close = series_.close(i);
        std::int64_t target = static_cast<std::int64_t>(
                cash_ / (close * (1 + model_.worstCaseCostFraction())));
        if (target <= 0) {
            return;
        }
        parents_.emplace_back(Side::BUY, i, close, ParentOrder::REASON_ENTRY);
        entry_ = static_cast<int>(parents_.size()) - 1;
        entryCost_ = 0;
        exitProceeds_ = 0;
        totalEntryQty_ = 0;
        firstEntryBar_ = -1;
        pendingEntry_ = target;
        model_.onParentOrder(Side::BUY, target, i);
        workEntry(i);
    } else if (signal == Signal::SELL && pendingExit_ == 0
               && (position_ > 0 || pendingEntry_ > 0)) {
        pendingEntry_ = 0;  // cancel unfilled entry remainder
        if (position_ > 0) {
            startExit(i, Trade::REASON_SIGNAL);
            workExit(i);
        } else {
            entry_ = -1;  // nothing filled; abandon the parent
        }
    }
}

void ExecutionAwareBacktester::startExit(int i, const char* reason) {
    pendingEntry_ = 0;
    parents_.emplace_back(Side::SELL, i, series_.close(i), reason);
    exit_ = static_cast<int>(parents_.size()) - 1;
    pendingExit_ = position_;
    model_.onParentOrder(Side::SELL, position_, i);
}

void ExecutionAwareBacktester::closeRoundTrip(int i) {
    double avgEntry = entryCost_ / static_cast<double>(totalEntryQty_);
    double avgExit = exitProceeds_ / static_cast<double>(totalEntryQty_);
    double pnl = exitProceeds_ - entryCost_;
    trades_.push_back(Trade{series_.symbol(), firstEntryBar_, i,
                            series_.timestamp(firstEntryBar_), series_.timestamp(i), avgEntry,
                            avgExit, static_cast<double>(totalEntryQty_), pnl,
                            entryCost_ == 0 ? 0 : pnl / entryCost_,
                            parents_[static_cast<std::size_t>(exit_)].reason});
    entry_ = -1;
    exit_ = -1;
    firstEntryBar_ = -1;
    entryCost_ = 0;
    exitProceeds_ = 0;
    totalEntryQty_ = 0;
}

void ExecutionAwareBacktester::forceClose(int last, std::vector<double>& equity) {
    pendingEntry_ = 0;
    if (position_ <= 0) {
        return;
    }
    if (exit_ < 0) {
        parents_.emplace_back(Side::SELL, last, series_.close(last), Trade::REASON_END_OF_DATA);
        exit_ = static_cast<int>(parents_.size()) - 1;
    }
    // Even a forced close pays to trade: charge the model's worst-case
    // cost rather than exiting for free — a run that ends holding must
    // not get its last round trip's exit cost waived.
    double px = series_.close(last) * (1 - model_.worstCaseCostFraction());
    Execution fill{series_.symbol(), Side::SELL, px, position_, series_.timestamp(last),
                   "FORCED_CLOSE"};
    ParentState& exit = parents_[static_cast<std::size_t>(exit_)];
    exit.fills.push_back(fill);
    exit.bars.push_back(last);
    cash_ += px * static_cast<double>(position_);
    exitProceeds_ += px * static_cast<double>(position_);
    position_ = 0;
    pendingExit_ = 0;
    closeRoundTrip(last);
    equity[static_cast<std::size_t>(last)] = cash_;
}

}  // namespace quantfinlib
