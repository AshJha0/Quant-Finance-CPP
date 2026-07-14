// Port of com.quantfinlib.backtest.LastLookExecution: last-look
// execution model — the missing realism for FX backtests. On ECN and
// single-dealer FX liquidity the provider holds your order briefly and
// may REJECT it if the price moves against them during the hold; rejects
// cluster exactly on the flow that was about to be profitable.
//
// Bar-level model of the hold window: the order arrives at the bar open
// and the LP watches the intra-bar move.
//
// Signal-bar handling: when worked through ExecutionAwareBacktester,
// the parent is created at the signal bar's CLOSE — so on that first
// bar there is no hold window left to observe, and filling at that
// bar's OPEN would credit a price from before the signal existed
// (intrabar time travel). The model therefore HOLDS on the parent's
// signal bar (no fill, no reject counted — it is pure latency, the LP
// has seen nothing yet); the first real attempt is the next bar, whose
// open is the price standing when the order actually arrived. Direct
// calls without onParentOrder keep the plain arrives-at-the-open
// semantics.
//
//  - Move in the taker's favor beyond rejectThresholdBps (price rising
//    on a buy — adverse to the LP who would sell) -> REJECT; the parent
//    quantity carries to the next bar, exactly like real
//    requote-and-chase.
//  - Otherwise -> full fill at the open plus the taker pays spreadBps
//    half-spread (all-in price).
//
// The asymmetry is deliberate AS A TAKER'S WORST-CASE MODEL: it
// simulates the adverse LP behavior the FX Global Code prohibits but a
// taker must still budget for. Rejection statistics are exposed for
// TCA — a live desk watches its reject rate per LP for exactly this
// pattern. Gates map to std::invalid_argument.
#pragma once

#include <stdexcept>

#include "quantfinlib/backtest/execution_model.hpp"

namespace quantfinlib {

class LastLookExecution final : public ExecutionModel {
public:
    /**
     * @param spreadBps          half-spread paid on accepted fills
     * @param rejectThresholdBps intra-bar move (bps, in the taker's
     *                           favor) beyond which the LP rejects
     */
    LastLookExecution(double spreadBps, double rejectThresholdBps)
            : spreadBps_(spreadBps), rejectThresholdBps_(rejectThresholdBps) {
        if (spreadBps < 0 || rejectThresholdBps <= 0) {
            throw std::invalid_argument("spreadBps must be >= 0 and rejectThresholdBps > 0");
        }
    }

    void onParentOrder(Side /*side*/, std::int64_t /*totalQuantity*/, int signalIndex) override {
        parentSignalBar_ = signalIndex;
    }

    double referencePrice(const BarSeries& series, int index) const override {
        // Fills anchor to the bar OPEN, not the close: sizing against
        // the close would overdraw cash on any accepted bar that gaps
        // open-high (the engine budgets request * referencePrice *
        // (1 + spread)).
        return series.open(index);
    }

    double worstCaseCostFraction() const override {
        // Exact: the all-in fill is open * (1 + spread), and
        // referencePrice hands the engine that same open.
        return spreadBps_ / 1e4;
    }

    std::vector<Execution> execute(Side side, std::int64_t requestedQty, const BarSeries& series,
                                   int index) override {
        if (requestedQty <= 0) {
            return {};
        }
        if (index == parentSignalBar_) {
            // Order decided at this bar's close: no hold window has
            // elapsed and this bar's open predates the signal. Hold —
            // not a reject.
            return {};
        }
        double open = series.open(index);
        double close = series.close(index);
        // Signed move in the TAKER's favor: up for a buy, down for a sell.
        double favorableMoveBps = sign(side) * (close - open) / open * 1e4;
        if (favorableMoveBps > rejectThresholdBps_) {
            // The LP saw the market run away and pulled the quote. The
            // engine retries the remainder on later bars — chasing the
            // move.
            rejects_++;
            return {};
        }
        fills_++;
        double allIn = open * (1 + sign(side) * spreadBps_ / 1e4);
        return {Execution{series.symbol(), side, allIn, requestedQty, series.timestamp(index),
                          "LASTLOOK"}};
    }

    /** Accepted fills (parent-bar attempts, not shares). */
    std::int64_t fillCount() const { return fills_; }

    /** Last-look rejections — the number a real desk tracks per LP. */
    std::int64_t rejectCount() const { return rejects_; }

    /** Reject rate across all attempts, 0 when nothing was attempted. */
    double rejectRate() const {
        std::int64_t attempts = fills_ + rejects_;
        return attempts == 0 ? 0 : static_cast<double>(rejects_) / attempts;
    }

private:
    double spreadBps_;
    double rejectThresholdBps_;

    std::int64_t fills_ = 0;
    std::int64_t rejects_ = 0;
    int parentSignalBar_ = -1;
};

}  // namespace quantfinlib
