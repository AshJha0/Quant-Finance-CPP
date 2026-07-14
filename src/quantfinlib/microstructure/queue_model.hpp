// Port of com.quantfinlib.microstructure.QueueModel: queue positioning and
// priority analytics -- how position in the price-time queue (and small
// latency differences reaching it) translate into fill probability.
// Executed volume at a price level over a horizon is treated as
// exponentially distributed with mean expectedTradedQty; an order fills
// when cumulative executions reach the quantity ahead of it plus its own
// size. Free functions, no state.
#pragma once

#include <cstdint>

namespace quantfinlib {
namespace queue_model {

/** P(fill) = exp(-(qtyAhead + orderQty) / expectedTradedQty); 0 if expectedTradedQty <= 0. */
double fillProbability(std::int64_t qtyAhead, std::int64_t orderQty, double expectedTradedQty);

/** Extra quantity joining ahead of an order arriving latencyNanos later. */
double queueGrowth(double joinRateQtyPerSec, std::int64_t latencyNanos);

/** Fill-probability edge from being latencyAdvantageNanos faster to the queue. */
double latencyFillAdvantage(std::int64_t qtyAhead, std::int64_t orderQty,
                             double expectedTradedQty, double joinRateQtyPerSec,
                             std::int64_t latencyAdvantageNanos);

}  // namespace queue_model
}  // namespace quantfinlib
