#include "quantfinlib/fx/fixing_risk.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {
namespace fixing_risk {

double windowTwap(const std::vector<double>& prices) {
    if (prices.empty()) {
        throw std::invalid_argument("empty window");
    }
    double sum = 0;
    for (double p : prices) {
        sum += p;
    }
    return sum / static_cast<double>(prices.size());
}

double windowVwap(const std::vector<double>& prices, const std::vector<double>& sizes) {
    if (prices.empty() || prices.size() != sizes.size()) {
        throw std::invalid_argument("prices/sizes must be non-empty and aligned");
    }
    double pv = 0;
    double v = 0;
    for (std::size_t i = 0; i < prices.size(); i++) {
        pv += prices[i] * sizes[i];
        v += sizes[i];
    }
    if (v <= 0) {
        throw std::invalid_argument("window volume must be > 0");
    }
    return pv / v;
}

double slippageVsFix(const CurrencyPair& pair, double achievedPrice, double fixPrice) {
    return pair.pips(achievedPrice - fixPrice);
}

double trackingErrorStd(double volPerSqrtMinute, double windowMinutes) {
    if (volPerSqrtMinute < 0 || windowMinutes <= 0) {
        throw std::invalid_argument("vol must be >= 0 and window > 0");
    }
    return volPerSqrtMinute * std::sqrt(windowMinutes / 3.0);
}

double participationRate(double orderQty, double expectedWindowVolume) {
    if (expectedWindowVolume <= 0) {
        throw std::invalid_argument("expected window volume must be > 0");
    }
    return std::fabs(orderQty) / expectedWindowVolume;
}

}  // namespace fixing_risk
}  // namespace quantfinlib
