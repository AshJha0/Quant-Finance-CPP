// ExecutionModel that fills an ExecutionAwareBacktester's orders through
// QueueFillSimulator, from a tick tape recorded per bar. Each bar's request
// goes out when the bar's tape starts and whatever has not filled by the end
// of the tape is cancelled, so the engine retries it next bar. A bar with no
// tape fills nothing.
//
// AGGRESSIVE takes displayed depth with a limit at the engine's cost budget.
// PASSIVE joins the near touch and waits for the queue.
//
// Fill prices are all-in: feeBps is folded in, as the seam requires.
#pragma once

#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

#include "quantfinlib/backtest/execution_model.hpp"
#include "quantfinlib/execution/queue_fill_simulator.hpp"

namespace quantfinlib {

/** One event on a tick tape: a top of book, or a print when isTrade is set. */
struct TapeEvent {
    std::int64_t nanos = 0;
    bool isTrade = false;
    double bid = 0;
    std::int64_t bidSize = 0;
    double ask = 0;
    std::int64_t askSize = 0;
    double tradePrice = 0;
    std::int64_t tradeQty = 0;
};

class QueueExecution : public ExecutionModel {
public:
    enum class Style { AGGRESSIVE, PASSIVE };

    /** @param tape events by bar index, time ordered across bars */
    QueueExecution(std::map<int, std::vector<TapeEvent>> tape, Style style, double feeBps,
                   const QueueFillSimulator::Config& config = QueueFillSimulator::Config())
            : tape_(std::move(tape)), style_(style), feeBps_(feeBps), simulator_(config) {}

    std::vector<Execution> execute(Side side, std::int64_t requestedQty, const BarSeries& series,
                                   int index) override {
        std::vector<Execution> fills;
        auto it = tape_.find(index);
        if (it == tape_.end() || it->second.empty() || requestedQty <= 0) {
            return fills;
        }
        const std::vector<TapeEvent>& events = it->second;
        std::size_t next = 0;
        // The first book opens the bar; the order is decided on it, not on anything later.
        while (next < events.size() && events[next].isTrade) {
            next++;
        }
        if (next == events.size()) {
            return fills;
        }
        apply(events[next]);
        const TapeEvent& open = events[next++];
        double budget = referencePrice(series, index) * (1 + sign(side) * worstCaseCostFraction());
        double net = budget / (1 + sign(side) * feeBps_ / 1e4);
        double limit = style_ == Style::PASSIVE ? (side == Side::BUY ? open.bid : open.ask) : net;
        std::int64_t id = simulator_.submitLimit(open.nanos, side, requestedQty, limit);
        for (; next < events.size(); next++) {
            apply(events[next]);
        }
        std::int64_t end = events.back().nanos;
        simulator_.cancel(end, id);
        // Everything sent has an outcome once the cancel lands and comes back.
        std::int64_t settled = end + 2 * (simulator_.config().orderLatencyNanos
                                          + simulator_.config().orderLatencyJitterNanos);
        simulator_.advanceTo(settled);
        for (const auto& r : simulator_.drainReports(settled)) {
            if (r.type == QueueFillSimulator::ReportType::FILL && r.orderId == id) {
                fills.push_back(Execution{series.symbol(), side,
                                          r.price * (1 + sign(side) * feeBps_ / 1e4), r.quantity,
                                          r.exchangeNanos, "SIM"});
            }
        }
        return fills;
    }

    const QueueFillSimulator& simulator() const { return simulator_; }

private:
    void apply(const TapeEvent& e) {
        // A settle step may have run past the next bar's first stamps.
        std::int64_t nanos = std::max(e.nanos, simulator_.clockNanos());
        if (e.isTrade) {
            simulator_.onTrade(nanos, e.tradePrice, e.tradeQty);
        } else {
            simulator_.onBook(nanos, e.bid, e.bidSize, e.ask, e.askSize);
        }
    }

    std::map<int, std::vector<TapeEvent>> tape_;
    Style style_;
    double feeBps_;
    QueueFillSimulator simulator_;
};

}  // namespace quantfinlib
