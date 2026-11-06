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
    bool nullGate = false;
    for (HftRiskGate* gate : gates_) {
        nullGate = nullGate || gate == nullptr;
    }
    if (gates_.empty() || nullGate || !(maxGrossNotional > 0) || !(resumeFraction > 0)
            || !(resumeFraction < 1) || pollIntervalNanos <= 0) {
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
                if (ref == ref) {  // unpriced symbols are caught by unpricedPositions()
                    gross += std::fabs(static_cast<double>(position)) * ref;
                }
            }
        }
    }
    return gross;
}

int GlobalRiskAggregator::unpricedPositions() const {
    int unpriced = 0;
    for (HftRiskGate* gate : gates_) {
        int n = gate->symbolCapacity();
        for (int s = 0; s < n; s++) {
            double ref = gate->referencePrice(s);
            if (gate->position(s) != 0 && ref != ref) {
                unpriced++;
            }
        }
    }
    return unpriced;
}

bool GlobalRiskAggregator::manualReset() {
    if (!(grossNotional() < resumeNotional_) || unpricedPositions() != 0) {
        return false;
    }
    for (HftRiskGate* gate : gates_) {
        gate->reset(HftRiskGate::KILL_AGGREGATOR);
    }
    tripped_.store(false, std::memory_order_release);
    return true;
}

void GlobalRiskAggregator::monitorLoop() {
    while (running_.load(std::memory_order_acquire)) {
        double gross = grossNotional();
        lastGross_.store(gross, std::memory_order_release);
        // !(gross <= cap) so that a NaN sum trips too.
        if (!tripped_.load(std::memory_order_acquire)
                && (!(gross <= maxGrossNotional_) || unpricedPositions() != 0)) {
            for (HftRiskGate* gate : gates_) {
                gate->trip(HftRiskGate::KILL_AGGREGATOR);
            }
            tripped_.store(true, std::memory_order_release);
            trips_.fetch_add(1, std::memory_order_acq_rel);
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
