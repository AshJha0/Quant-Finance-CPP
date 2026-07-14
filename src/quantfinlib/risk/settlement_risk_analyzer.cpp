#include "quantfinlib/risk/settlement_risk_analyzer.hpp"

#include <algorithm>

namespace quantfinlib {
namespace settlement_risk_analyzer {

std::vector<std::pair<std::string, double>> herstattExposure(
        const std::vector<SettlementLeg>& legs) {
    std::vector<std::pair<std::string, double>> out;
    for (const auto& leg : legs) {
        if (!leg.hasHerstattWindow()) {
            continue;
        }
        bool merged = false;
        for (auto& entry : out) {
            if (entry.first == leg.counterparty) {
                entry.second += leg.receiveAmount;
                merged = true;
                break;
            }
        }
        if (!merged) {
            out.emplace_back(leg.counterparty, leg.receiveAmount);
        }
    }
    return out;
}

double peakExposure(const std::vector<SettlementLeg>& legs,
                    const std::string& counterparty) {
    struct Event {
        std::int64_t time;
        double delta;
    };
    std::vector<Event> events;
    for (const auto& leg : legs) {
        if (leg.counterparty != counterparty || !leg.hasHerstattWindow()) {
            continue;
        }
        events.push_back({leg.payTimeMillis, leg.receiveAmount});
        events.push_back({leg.receiveTimeMillis, -leg.receiveAmount});
    }
    // At equal timestamps apply PAYMENTS (positive deltas) before
    // receipts: for a worst-case exposure metric the conservative
    // reading of a simultaneous pay/receive is that your money left
    // first. Receipts-first would quietly understate the peak — the
    // wrong direction for a number named after Herstatt.
    std::stable_sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
        if (a.time != b.time) {
            return a.time < b.time;
        }
        return a.delta > b.delta;  // descending delta at equal times
    });
    double outstanding = 0;
    double peak = 0;
    for (const auto& e : events) {
        outstanding += e.delta;
        peak = std::max(peak, outstanding);
    }
    return peak;
}

}  // namespace settlement_risk_analyzer
}  // namespace quantfinlib
