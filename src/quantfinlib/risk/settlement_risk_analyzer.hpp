// Port of com.quantfinlib.risk.SettlementRiskAnalyzer (Java): settlement
// (Herstatt) risk — the exposure created when one currency is paid away
// before the other leg is received. herstattExposure sums at-risk
// receive amounts per counterparty (insertion order preserved, matching
// Java's LinkedHashMap); peakExposure sweeps pay/receive events with
// payments applied BEFORE receipts at equal timestamps (the conservative
// reading — receipts-first would understate the peak).
#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace quantfinlib {
namespace settlement_risk_analyzer {

/** One settlement instruction pair: we pay one leg and receive the other. */
struct SettlementLeg {
    std::string counterparty;
    std::string payCurrency;
    double payAmount;
    std::int64_t payTimeMillis;
    std::string receiveCurrency;
    double receiveAmount;
    std::int64_t receiveTimeMillis;

    /** True when we pay before we receive — the Herstatt window. */
    bool hasHerstattWindow() const { return payTimeMillis < receiveTimeMillis; }
};

/**
 * Total at-risk receive amounts per counterparty: sum of legs where
 * payment goes out before the countervalue arrives.
 */
std::vector<std::pair<std::string, double>> herstattExposure(
        const std::vector<SettlementLeg>& legs);

/**
 * Peak intraday settlement exposure to one counterparty: the maximum
 * total receive-amount outstanding (paid but not yet received) at any
 * point in time.
 */
double peakExposure(const std::vector<SettlementLeg>& legs,
                    const std::string& counterparty);

}  // namespace settlement_risk_analyzer
}  // namespace quantfinlib
