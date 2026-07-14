#include "quantfinlib/credit/credit_spreads.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace credit_spreads {

namespace {

double pv(double face, double couponRate, int frequency,
          double yearsToMaturity, const YieldCurve& curve, double z) {
    int n = static_cast<int>(std::lround(yearsToMaturity * frequency));
    double coupon = face * couponRate / frequency;
    double value = 0;
    for (int i = 1; i <= n; i++) {
        double t = static_cast<double>(i) / frequency;
        double cf = coupon + (i == n ? face : 0);
        value += cf * std::exp(-(curve.zeroRate(t) + z) * t);
    }
    return value;
}

}  // namespace

double zSpread(double dirtyPrice, double face, double couponRate,
               int frequency, double yearsToMaturity, const YieldCurve& curve) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    if (!(dirtyPrice > 0) || dirtyPrice == kInf) {
        throw std::invalid_argument("dirtyPrice must be positive and finite");
    }
    if (!(face > 0) || face == kInf) {
        throw std::invalid_argument("face must be positive and finite");
    }
    if (!(couponRate >= 0) || couponRate == kInf) {
        throw std::invalid_argument("couponRate must be >= 0 and finite");
    }
    if (frequency < 1) {
        throw std::invalid_argument("frequency must be >= 1, got "
                                    + std::to_string(frequency));
    }
    if (!(yearsToMaturity > 0) || yearsToMaturity == kInf) {
        throw std::invalid_argument("yearsToMaturity must be positive and finite");
    }
    double lo = -0.5, hi = 5.0;
    double pvLo = pv(face, couponRate, frequency, yearsToMaturity, curve, lo);
    double pvHi = pv(face, couponRate, frequency, yearsToMaturity, curve, hi);
    // PV is decreasing in z: pv(lo) is the maximum, pv(hi) the minimum.
    if (!(dirtyPrice <= pvLo) || !(dirtyPrice >= pvHi)) {
        throw std::invalid_argument("price " + std::to_string(dirtyPrice)
                + " has no Z-spread in [-50%, 500%] (attainable ["
                + std::to_string(pvHi) + ", " + std::to_string(pvLo) + "])");
    }
    for (int i = 0; i < 200; i++) {
        double mid = 0.5 * (lo + hi);
        if (pv(face, couponRate, frequency, yearsToMaturity, curve, mid) > dirtyPrice) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

double priceWithZSpread(double face, double couponRate, int frequency,
                        double yearsToMaturity, const YieldCurve& curve, double z) {
    if (!std::isfinite(z)) {
        throw std::invalid_argument("z must be finite");
    }
    return pv(face, couponRate, frequency, yearsToMaturity, curve, z);
}

}  // namespace credit_spreads
}  // namespace quantfinlib
