// Port of com.quantfinlib.backtest.ExecutionAwareBacktester:
// execution-aware backtesting engine — strategy signals create PARENT
// ORDERS that are worked through an ExecutionModel (sliced by
// IcebergExecution, held by LastLookExecution, or filled instantly by
// InstantExecution). Fills can span multiple bars, the position
// accumulates gradually, and every child fill is recorded so execution
// cost (TCA) is measurable per parent order.
//
// Semantics (long-only, single instrument, like Backtester):
//  - config.fillTiming: by default a parent decided on bar t's close is
//    first worked on bar t+1; SAME_BAR_CLOSE works it on the signal bar.
//    A risk exit triggered inside a bar follows the same rule, because a
//    model anchored to that bar's open would otherwise fill before the
//    trigger happened. Use InstantExecution::from(config) to match.
//  - Each bar's child requests are capped at
//    config.maxParticipationRate of that bar's volume.
//  - BUY signal while flat -> entry parent sized to available cash;
//    worked until filled or superseded.
//  - SELL signal -> cancels any unfilled entry remainder and works an
//    exit parent for the whole position.
//  - Stop-loss / take-profit are evaluated intrabar against the
//    volume-weighted average entry price; the triggered exit is worked
//    through the execution model (a patient model exits slowly — that
//    realism is the point).
//  - Any position left at the end of data is force-closed at the last
//    close less the model's worst-case cost fraction — unconditional
//    (it bypasses the model's fill logic) but not free.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/execution_aware_result.hpp"
#include "quantfinlib/backtest/execution_model.hpp"
#include "quantfinlib/backtest/trading_strategy.hpp"
#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {

class ExecutionAwareBacktester {
public:
    static ExecutionAwareResult run(TradingStrategy& strategy, const BarSeries& series,
                                    const BacktestConfig& config, ExecutionModel& model);

private:
    struct ParentState {
        Side side = Side::BUY;
        int signalIndex = 0;
        double arrivalPrice = 0;
        std::string reason;
        std::vector<Execution> fills;
        std::vector<int> bars;

        ParentState() = default;
        ParentState(Side s, int idx, double arrival, std::string r)
                : side(s), signalIndex(idx), arrivalPrice(arrival), reason(std::move(r)) {}

        ParentOrder toRecord() const {
            return ParentOrder{side, signalIndex, arrivalPrice, reason, fills, bars};
        }
    };

    ExecutionAwareBacktester(TradingStrategy& strategy, const BarSeries& series,
                             const BacktestConfig& config, ExecutionModel& model);

    ExecutionAwareResult execute();

    void workEntry(int i);
    void workExit(int i);
    void checkRiskExits(int i);
    void onSignal(Signal signal, int i);
    void startExit(int i, const char* reason);
    void closeRoundTrip(int i);
    void forceClose(int last, std::vector<double>& equity);
    std::int64_t volumeLimited(std::int64_t qty, int i) const;

    TradingStrategy& strategy_;
    const BarSeries& series_;
    const BacktestConfig& config_;
    ExecutionModel& model_;
    double stopLossPct_;
    double takeProfitPct_;
    bool sameBar_;
    bool volumeKnown_;

    std::vector<Trade> trades_;
    // Parents are stored by value; entry_/exit_ index into parents_
    // (-1 = none) — the Java version shares mutable ParentState objects
    // by reference instead.
    std::vector<ParentState> parents_;
    double cash_ = 0;
    std::int64_t position_ = 0;
    std::int64_t pendingEntry_ = 0;
    std::int64_t pendingExit_ = 0;
    double entryCost_ = 0;  // all-in cash spent on the current round trip's entries
    double exitProceeds_ = 0;
    std::int64_t totalEntryQty_ = 0;
    int firstEntryBar_ = -1;
    int entry_ = -1;  // index into parents_, -1 = none
    int exit_ = -1;   // index into parents_, -1 = none
};

}  // namespace quantfinlib
