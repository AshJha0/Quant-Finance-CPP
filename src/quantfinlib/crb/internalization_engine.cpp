#include "quantfinlib/crb/internalization_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {
double signum(double x) {
    return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
}
}  // namespace

InternalizationEngine::InternalizationEngine(double warehouseLimit, double improvementShare)
    : warehouseLimit_(warehouseLimit), improvementShare_(improvementShare) {
    if (!(warehouseLimit > 0) || warehouseLimit == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("warehouseLimit must be positive and finite");
    }
    if (!(improvementShare >= 0 && improvementShare <= 1)) {
        throw std::invalid_argument("improvementShare must be in [0, 1]");
    }
}

InternalizationEngine::Decision InternalizationEngine::decide(double bookNet, double flow,
                                                               double halfSpreadBps) {
    if (!std::isfinite(bookNet) || !std::isfinite(flow) || flow == 0) {
        throw std::invalid_argument("bookNet/flow must be finite, flow non-zero");
    }
    if (!(halfSpreadBps > 0) || halfSpreadBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpreadBps must be positive and finite");
    }
    double internalized;
    double improvement = 0;
    if (bookNet != 0 && signum(flow) != signum(bookNet)) {
        double reducing = std::min(std::fabs(flow), std::fabs(bookNet));
        improvement = improvementShare_ * halfSpreadBps;
        double excess = std::fabs(flow) - reducing;
        double warehoused = std::min(excess, warehouseLimit_);
        internalized = signum(flow) * (reducing + warehoused);
        if (warehoused > 0 && excess > 0) {
            improvement = improvement * reducing / (reducing + warehoused);
        }
    } else {
        double headroom = std::max(0.0, warehouseLimit_ - std::fabs(bookNet));
        internalized = signum(flow) * std::min(std::fabs(flow), headroom);
    }
    double routed = flow - internalized;
    internalizedNotional_ += std::fabs(internalized);
    routedNotional_ += std::fabs(routed);
    return {internalized, routed, improvement};
}

double InternalizationEngine::internalizationRate() const {
    double total = internalizedNotional_ + routedNotional_;
    return total <= 0 ? 0 : internalizedNotional_ / total;
}

}  // namespace quantfinlib
