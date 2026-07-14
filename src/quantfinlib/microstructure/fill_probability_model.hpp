// Port of com.quantfinlib.microstructure.FillProbabilityModel: passive-fill
// probability for a limit order resting away from the touch -- touch
// probability (reflection-principle barrier touch under driftless
// diffusion) times QueueModel's queue-clear probability, under an
// independence approximation (a mild, documented underestimate). Static,
// no state.
#pragma once

#include <cstdint>

namespace quantfinlib {
namespace fill_probability_model {

/**
 * Probability the price touches a level `distance` away (price units, >= 0)
 * within horizonSeconds, given volPerSqrtSecond (return per sqrt(second))
 * at `price`. 1 at/through the level; 0 for degenerate inputs.
 */
double touchProbability(double distance, double volPerSqrtSecond, double horizonSeconds,
                         double price);

/** touchProbability * QueueModel::fillProbability (independence approximation). */
double passiveFillProbability(double distance, double volPerSqrtSecond, double horizonSeconds,
                               double price, std::int64_t qtyAhead, std::int64_t orderQty,
                               double expectedTradedQty);

}  // namespace fill_probability_model
}  // namespace quantfinlib
