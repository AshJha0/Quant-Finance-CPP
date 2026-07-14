#include "quantfinlib/microstructure/queue_model.hpp"

#include <algorithm>
#include <cmath>

namespace quantfinlib {
namespace queue_model {

double fillProbability(std::int64_t qtyAhead, std::int64_t orderQty, double expectedTradedQty) {
    if (expectedTradedQty <= 0) {
        return 0;
    }
    return std::exp(-static_cast<double>(qtyAhead + orderQty) / expectedTradedQty);
}

double queueGrowth(double joinRateQtyPerSec, std::int64_t latencyNanos) {
    return std::max(0.0, joinRateQtyPerSec) * static_cast<double>(latencyNanos) / 1e9;
}

double latencyFillAdvantage(std::int64_t qtyAhead, std::int64_t orderQty,
                             double expectedTradedQty, double joinRateQtyPerSec,
                             std::int64_t latencyAdvantageNanos) {
    double fast = fillProbability(qtyAhead, orderQty, expectedTradedQty);
    std::int64_t extra =
            static_cast<std::int64_t>(std::llround(queueGrowth(joinRateQtyPerSec, latencyAdvantageNanos)));
    double slow = fillProbability(qtyAhead + extra, orderQty, expectedTradedQty);
    return fast - slow;
}

}  // namespace queue_model
}  // namespace quantfinlib
