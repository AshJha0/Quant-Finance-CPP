#include "quantfinlib/execution/order_placement_policy.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {
namespace order_placement_policy {

namespace {
constexpr PostRegion NEVER{1, 0};
}  // namespace

Placement decide(double halfSpread, double fillProbability, double adverseSelection, double adverseDrift,
                 double rebate) {
    if (!(halfSpread > 0) || halfSpread == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpread must be positive and finite");
    }
    if (!(fillProbability >= 0 && fillProbability <= 1)) {
        throw std::invalid_argument("fillProbability must be in [0, 1]");
    }
    if (!(adverseSelection >= 0) || adverseSelection == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("adverseSelection must be >= 0 and finite");
    }
    if (!std::isfinite(adverseDrift)) {
        throw std::invalid_argument("adverseDrift must be finite");
    }
    if (!(rebate >= 0) || rebate == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("rebate must be >= 0 and finite");
    }
    double postCost = fillProbability * (adverseSelection - halfSpread - rebate)
            + (1 - fillProbability) * (halfSpread + adverseDrift);
    return Placement{postCost < halfSpread, postCost, halfSpread};
}

PostRegion postRegion(double halfSpread, double adverseSelection, double adverseDrift, double rebate) {
    if (!(halfSpread > 0) || halfSpread == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpread must be positive and finite");
    }
    if (!(adverseSelection >= 0) || adverseSelection == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("adverseSelection must be >= 0 and finite");
    }
    if (!std::isfinite(adverseDrift)) {
        throw std::invalid_argument("adverseDrift must be finite");
    }
    if (!(rebate >= 0) || rebate == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("rebate must be >= 0 and finite");
    }
    // postCost(p) = (h + d) - p*coef;  post iff postCost < h  <=>  d < p*coef.
    double coef = 2 * halfSpread + rebate + adverseDrift - adverseSelection;
    if (coef > 0) {
        double pStar = adverseDrift / coef;
        return pStar >= 1 ? NEVER : PostRegion{std::max(0.0, pStar), 1};
    }
    if (coef < 0) {
        // The slope flipped: posting pays only BELOW the threshold.
        double pStar = adverseDrift / coef;
        return pStar <= 0 ? NEVER : PostRegion{0, std::min(1.0, pStar)};
    }
    // Flat in p: the sign of the drift decides for every p at once.
    return adverseDrift < 0 ? PostRegion{0, 1} : NEVER;
}

}  // namespace order_placement_policy
}  // namespace quantfinlib
