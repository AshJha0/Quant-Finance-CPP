#include "quantfinlib/microstructure/fill_probability_model.hpp"

#include <cmath>
#include <limits>

#include "quantfinlib/microstructure/queue_model.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace fill_probability_model {

double touchProbability(double distance, double volPerSqrtSecond, double horizonSeconds,
                         double price) {
    if (distance <= 0) {
        return 1.0;
    }
    double inf = std::numeric_limits<double>::infinity();
    if (!(volPerSqrtSecond > 0) || !(horizonSeconds > 0) || !(price > 0)
        || volPerSqrtSecond == inf || distance == inf) {
        return 0.0;
    }
    double sigmaAbs = price * volPerSqrtSecond * std::sqrt(horizonSeconds);
    return math_utils::clamp(2 * math_utils::normCdf(-distance / sigmaAbs), 0, 1);
}

double passiveFillProbability(double distance, double volPerSqrtSecond, double horizonSeconds,
                               double price, std::int64_t qtyAhead, std::int64_t orderQty,
                               double expectedTradedQty) {
    double touch = touchProbability(distance, volPerSqrtSecond, horizonSeconds, price);
    return touch * queue_model::fillProbability(qtyAhead, orderQty, expectedTradedQty);
}

}  // namespace fill_probability_model
}  // namespace quantfinlib
