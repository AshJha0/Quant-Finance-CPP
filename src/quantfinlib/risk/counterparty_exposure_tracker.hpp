// Port of com.quantfinlib.risk.CounterpartyExposureTracker (Java):
// counterparty credit exposure with netting. Current exposure is
// max(0, net MTM) per netting set; PFE uses BIS CEM-style FX add-ons
// (<1y 1%, 1-5y 5%, >5y 7.5%) with the net-to-gross adjustment
// PFE = (0.4 + 0.6*NGR) * grossAddOn, NGR = 1 (no relief) when there is
// no positive MTM to net against. Java's LinkedHashMap maps to a vector
// keyed by counterparty in insertion order.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace quantfinlib {

class CounterpartyExposureTracker {
public:
    struct CounterpartyTrade {
        std::string counterparty;
        std::string product;
        double notional;
        double markToMarket;
        double tenorYears;
    };

    CounterpartyExposureTracker& addTrade(const CounterpartyTrade& trade);

    /** Net current exposure (MTM netted within the counterparty netting set, floored at 0). */
    double currentExposure(const std::string& counterparty) const;

    /** Potential future exposure with the CEM net-to-gross adjustment. */
    double potentialFutureExposure(const std::string& counterparty) const;

    /** Total exposure = current + potential future. */
    double totalExposure(const std::string& counterparty) const;

    /** Total exposure per counterparty (insertion order preserved). */
    std::vector<std::pair<std::string, double>> allExposures() const;

    /** BIS CEM-style FX add-on factor by residual tenor. */
    static double addOnFactor(double tenorYears);

private:
    std::vector<std::pair<std::string, std::vector<CounterpartyTrade>>> byCounterparty_;

    const std::vector<CounterpartyTrade>& trades(const std::string& counterparty) const;
};

}  // namespace quantfinlib
