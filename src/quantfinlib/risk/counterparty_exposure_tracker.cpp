#include "quantfinlib/risk/counterparty_exposure_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace quantfinlib {

CounterpartyExposureTracker& CounterpartyExposureTracker::addTrade(
        const CounterpartyTrade& trade) {
    for (auto& entry : byCounterparty_) {
        if (entry.first == trade.counterparty) {
            entry.second.push_back(trade);
            return *this;
        }
    }
    byCounterparty_.emplace_back(trade.counterparty,
                                 std::vector<CounterpartyTrade>{trade});
    return *this;
}

double CounterpartyExposureTracker::currentExposure(
        const std::string& counterparty) const {
    double net = 0;
    for (const auto& t : trades(counterparty)) {
        net += t.markToMarket;
    }
    return std::max(0.0, net);
}

double CounterpartyExposureTracker::potentialFutureExposure(
        const std::string& counterparty) const {
    double grossAddOn = 0;
    double grossPositiveMtm = 0;
    for (const auto& t : trades(counterparty)) {
        grossAddOn += std::fabs(t.notional) * addOnFactor(t.tenorYears);
        grossPositiveMtm += std::max(0.0, t.markToMarket);
    }
    double ngr = grossPositiveMtm > 0
            ? currentExposure(counterparty) / grossPositiveMtm
            : 1.0;
    return (0.4 + 0.6 * ngr) * grossAddOn;
}

double CounterpartyExposureTracker::totalExposure(
        const std::string& counterparty) const {
    return currentExposure(counterparty) + potentialFutureExposure(counterparty);
}

std::vector<std::pair<std::string, double>>
CounterpartyExposureTracker::allExposures() const {
    std::vector<std::pair<std::string, double>> out;
    out.reserve(byCounterparty_.size());
    for (const auto& entry : byCounterparty_) {
        out.emplace_back(entry.first, totalExposure(entry.first));
    }
    return out;
}

double CounterpartyExposureTracker::addOnFactor(double tenorYears) {
    if (tenorYears < 1) {
        return 0.01;
    }
    if (tenorYears <= 5) {
        return 0.05;
    }
    return 0.075;
}

const std::vector<CounterpartyExposureTracker::CounterpartyTrade>&
CounterpartyExposureTracker::trades(const std::string& counterparty) const {
    static const std::vector<CounterpartyTrade> kEmpty;
    for (const auto& entry : byCounterparty_) {
        if (entry.first == counterparty) {
            return entry.second;
        }
    }
    return kEmpty;
}

}  // namespace quantfinlib
