#include "quantfinlib/microstructure/hidden_liquidity_detector.hpp"

#include <algorithm>
#include <stdexcept>

namespace quantfinlib {

HiddenLiquidityDetector::HiddenLiquidityDetector(int levels, double alpha)
        : levels_(levels), alpha_(alpha), displayed_(static_cast<std::size_t>(levels), 0),
          refillRatioEwma_(static_cast<std::size_t>(levels), 0.0),
          refillObs_(static_cast<std::size_t>(levels), 0) {
    if (levels < 1 || alpha <= 0 || alpha > 1) {
        throw std::invalid_argument("need levels >= 1, alpha in (0,1]");
    }
}

void HiddenLiquidityDetector::onDisplayed(int level, std::int64_t size) {
    displayed_[static_cast<std::size_t>(level)] = std::max<std::int64_t>(0, size);
}

void HiddenLiquidityDetector::onExecution(int level, std::int64_t qty) {
    if (qty <= 0) {
        return;
    }
    auto idx = static_cast<std::size_t>(level);
    std::int64_t shown = displayed_[idx];
    if (shown > 0 && qty > shown) {
        double ratio = static_cast<double>(qty) / static_cast<double>(shown);
        refillRatioEwma_[idx] = refillObs_[idx] == 0
                ? ratio
                : refillRatioEwma_[idx] + alpha_ * (ratio - refillRatioEwma_[idx]);
        refillObs_[idx]++;
    }
}

void HiddenLiquidityDetector::onLevelCleared(int level) {
    displayed_[static_cast<std::size_t>(level)] = 0;
}

double HiddenLiquidityDetector::hiddenMultiplier(int level) const {
    auto idx = static_cast<std::size_t>(level);
    return refillObs_[idx] == 0 ? 1.0 : std::max(1.0, refillRatioEwma_[idx]);
}

double HiddenLiquidityDetector::estimatedTrueDepth(int level) const {
    return static_cast<double>(displayed_[static_cast<std::size_t>(level)]) * hiddenMultiplier(level);
}

bool HiddenLiquidityDetector::isIceberg(int level) const {
    auto idx = static_cast<std::size_t>(level);
    return refillObs_[idx] > 0 && refillRatioEwma_[idx] > 1.0;
}

}  // namespace quantfinlib
