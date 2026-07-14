// Port of com.quantfinlib.backtest.IcebergExecution: iceberg execution —
// wraps another ExecutionModel and caps each bar's execution at the
// IcebergOrder state machine's visible tranche (optionally randomized),
// plus an optional participation cap versus the bar's volume. A fresh
// iceberg is loaded for every parent order, so entries and exits are
// both worked patiently across bars.
//
// The wrapped model is held by reference: the caller keeps it alive for
// the lifetime of this wrapper (in Java both are garbage-collected
// references). Gates map to std::invalid_argument.
#pragma once

#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>

#include "quantfinlib/backtest/execution_model.hpp"
#include "quantfinlib/execution/iceberg_order.hpp"

namespace quantfinlib {

class IcebergExecution final : public ExecutionModel {
public:
    IcebergExecution(ExecutionModel& inner, std::int64_t displayQty)
            : IcebergExecution(inner, displayQty, 0, 0, 0) {}

    /**
     * @param randomizePct     display-size jitter, e.g. 0.2 = ±20%
     * @param maxParticipation cap per bar as a fraction of bar volume (0 = off)
     */
    IcebergExecution(ExecutionModel& inner, std::int64_t displayQty, double randomizePct,
                     double maxParticipation, std::int64_t seed)
            : inner_(&inner),
              displayQty_(displayQty),
              randomizePct_(randomizePct),
              maxParticipation_(maxParticipation),
              seed_(seed) {
        if (displayQty <= 0) {
            throw std::invalid_argument("displayQty must be positive");
        }
    }

    double worstCaseCostFraction() const override {
        // The INNER model prices the fills; a wrapper that reports the
        // 1% default while wrapping a costlier model re-opens the cash
        // overdraw the engine's sizing exists to prevent.
        return inner_->worstCaseCostFraction();
    }

    double referencePrice(const BarSeries& series, int index) const override {
        return inner_->referencePrice(series, index);
    }

    void onParentOrder(Side side, std::int64_t totalQuantity, int signalIndex) override {
        iceberg_.emplace(totalQuantity, displayQty_, randomizePct_,
                         seed_ + parentSeq_++ * 1000003LL);
        inner_->onParentOrder(side, totalQuantity, signalIndex);
    }

    std::vector<Execution> execute(Side side, std::int64_t requestedQty, const BarSeries& series,
                                   int index) override {
        if (!iceberg_.has_value() || iceberg_->isComplete() || requestedQty <= 0) {
            return {};
        }
        std::int64_t cap = std::min(requestedQty, iceberg_->visibleQty());
        if (maxParticipation_ > 0) {
            cap = std::min(cap,
                           static_cast<std::int64_t>(series.volume(index) * maxParticipation_));
        }
        if (cap <= 0) {
            return {};
        }
        std::vector<Execution> fills = inner_->execute(side, cap, series, index);
        std::int64_t filled = 0;
        for (const Execution& f : fills) {
            filled += f.quantity;
        }
        if (filled > 0) {
            iceberg_->onFill(filled);
        }
        return fills;
    }

private:
    ExecutionModel* inner_;
    std::int64_t displayQty_;
    double randomizePct_;
    double maxParticipation_;  // 0 = disabled
    std::int64_t seed_;
    std::optional<IcebergOrder> iceberg_;
    std::int64_t parentSeq_ = 0;
};

}  // namespace quantfinlib
