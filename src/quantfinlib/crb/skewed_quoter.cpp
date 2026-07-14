#include "quantfinlib/crb/skewed_quoter.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace skewed_quoter {

Quote quote(double mid, double halfSpreadBps, double inventory, double inventoryLimit,
           double skewFraction) {
    if (!(mid > 0) || mid == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("mid must be positive and finite");
    }
    if (!(halfSpreadBps > 0) || halfSpreadBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpreadBps must be positive and finite");
    }
    if (!(skewFraction >= 0 && skewFraction < 1)) {
        throw std::invalid_argument("skewFraction must be in [0, 1)");
    }
    // The worst-case downward shade is halfSpread*(1 + skewFraction): past
    // 10,000 bps that quotes a ZERO or NEGATIVE bid.
    if (halfSpreadBps * (1 + skewFraction) >= 10'000) {
        throw std::invalid_argument("halfSpreadBps with skewFraction could quote a non-positive bid");
    }
    if (!(inventoryLimit > 0) || inventoryLimit == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("inventoryLimit must be positive and finite");
    }
    if (!std::isfinite(inventory)) {
        throw std::invalid_argument("inventory must be finite");
    }
    double load = math_utils::clamp(inventory / inventoryLimit, -1, 1);
    double skewBps = -load * skewFraction * halfSpreadBps;
    double bid = mid * (1 + (-halfSpreadBps + skewBps) / 1e4);
    double ask = mid * (1 + (halfSpreadBps + skewBps) / 1e4);
    return {bid, ask, skewBps};
}

}  // namespace skewed_quoter
}  // namespace quantfinlib
