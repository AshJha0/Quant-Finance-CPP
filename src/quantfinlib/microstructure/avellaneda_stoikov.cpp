#include "quantfinlib/microstructure/avellaneda_stoikov.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {

AvellanedaStoikov::AvellanedaStoikov(double gamma, double kappa) : gamma_(gamma), kappa_(kappa) {
    if (gamma <= 0 || kappa <= 0) {
        throw std::invalid_argument("need gamma > 0 and kappa > 0");
    }
    // log1p keeps the liquidity floor exact as gamma -> 0.
    liquidityHalfSpread_ = std::log1p(gamma / kappa) / gamma;
}

double AvellanedaStoikov::reservationPrice(double mid, double inventory,
                                           double priceVariancePerSecond,
                                           double horizonSeconds) const {
    if (!std::isfinite(mid) || !std::isfinite(inventory)) {
        throw std::invalid_argument("mid and inventory must be finite");
    }
    return mid - inventory * gamma_ * varianceTerm(priceVariancePerSecond, horizonSeconds);
}

double AvellanedaStoikov::optimalHalfSpread(double priceVariancePerSecond,
                                            double horizonSeconds) const {
    return 0.5 * gamma_ * varianceTerm(priceVariancePerSecond, horizonSeconds) + liquidityHalfSpread_;
}

double AvellanedaStoikov::bidQuote(double mid, double inventory, double priceVariancePerSecond,
                                   double horizonSeconds) const {
    return reservationPrice(mid, inventory, priceVariancePerSecond, horizonSeconds)
            - optimalHalfSpread(priceVariancePerSecond, horizonSeconds);
}

double AvellanedaStoikov::askQuote(double mid, double inventory, double priceVariancePerSecond,
                                   double horizonSeconds) const {
    return reservationPrice(mid, inventory, priceVariancePerSecond, horizonSeconds)
            + optimalHalfSpread(priceVariancePerSecond, horizonSeconds);
}

double AvellanedaStoikov::varianceTerm(double priceVariancePerSecond, double horizonSeconds) {
    if (!(priceVariancePerSecond >= 0) || !(horizonSeconds >= 0)
        || !std::isfinite(priceVariancePerSecond) || !std::isfinite(horizonSeconds)) {
        throw std::invalid_argument("variance and horizon must be finite and >= 0");
    }
    return priceVariancePerSecond * horizonSeconds;
}

}  // namespace quantfinlib
