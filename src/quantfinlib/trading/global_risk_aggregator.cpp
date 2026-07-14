#include "quantfinlib/trading/global_risk_aggregator.hpp"

#include <cmath>
#include <chrono>
#include <stdexcept>

namespace quantfinlib {

GlobalRiskAggregator::GlobalRiskAggregator(std::vector<HftRiskGate*> gates,
                                           double maxGrossNotional, double resumeFraction,
                                           std::int64_t pollIntervalNanos)
        : gates_(std::move(gates)),
          maxGrossNotional_(maxGrossNotional),
          resumeNotional_(maxGrossNotional * resumeFraction),
          pollIntervalNanos_(pollIntervalNanos) {
    if (gates_.empty() || maxGrossNotional <= 0 || resumeFraction <= 0 || resumeFraction >= 1
            || pollIntervalNanos <= 0) {
        throw std::invalid_argument("need gates, cap > 0, 0 < resumeFraction < 1, poll > 0");
    }
    monitor_ = std::thread([this] { monitorLoop(); });
}

double GlobalRiskAggregator::grossNotional() const {
    double gross = 0;
    for (HftRiskGate* gate : gates_) {
        int n = gate->symbolCapacity();
        for (int s = 0; s < n; s++) {
            std::int64_t position = gate->position(s);
            if (position != 0) {
                double ref = gate->referencePrice(s);
                if (ref == ref) {  // NaN-safe: unpriced symbols contribute 0
                    gross += std::fabs(static_cast<double>(position)) * ref;
                }
            }
        }
    }
    return gross;
}

void GlobalRiskAggregator::monitorLoop() {
    while (running_.load(std::memory_order_acquire)) {
        double gross = grossNotional();
        lastGross_.store(gross, std::memory_order_release);
        if (!tripped_.load(std::memory_order_relaxed) && gross > maxGrossNotional_) {
            // Snapshot which gates were ALREADY killed (ops holds, a second
            // breaker): recovery must not clear a kill this aggregator did
            // not set.
            killedBeforeTrip_.assign(gates_.size(), false);
            for (std::size_t i = 0; i < gates_.size(); i++) {
                killedBeforeTrip_[i] = gates_[i]->isKilled();
                gates_[i]->kill(true);
            }
            tripped_.store(true, std::memory_order_release);
            trips_.fetch_add(1, std::memory_order_acq_rel);
        } else if (tripped_.load(std::memory_order_relaxed) && gross < resumeNotional_) {
            for (std::size_t i = 0; i < gates_.size(); i++) {
                if (!killedBeforeTrip_[i]) {
                    gates_[i]->kill(false);
                }
            }
            tripped_.store(false, std::memory_order_release);
        }
        std::this_thread::sleep_for(std::chrono::nanoseconds(pollIntervalNanos_));
    }
}

void GlobalRiskAggregator::close() {
    running_.store(false, std::memory_order_release);
    if (monitor_.joinable()) {
        monitor_.join();
    }
}

}  // namespace quantfinlib
