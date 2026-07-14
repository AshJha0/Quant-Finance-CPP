#include "quantfinlib/rates/key_rate_durations.hpp"

#include "quantfinlib/rates/bond_pricer.hpp"

namespace quantfinlib {
namespace key_rate_durations {

namespace {
constexpr double kBump = 1e-4;  // one basis point
}

std::vector<double> keyRateDv01s(double face, double couponRate, int frequency,
                                 double maturityYears, const YieldCurve& curve) {
    std::vector<double> tenors = curve.tenors();
    std::vector<double> baseRates(tenors.size());
    for (std::size_t i = 0; i < tenors.size(); i++) {
        baseRates[i] = curve.zeroRate(tenors[i]);
    }
    std::vector<double> krd(tenors.size());
    std::vector<double> bumped = baseRates;
    for (std::size_t i = 0; i < tenors.size(); i++) {
        bumped[i] = baseRates[i] + kBump;
        double up = bond_pricer::priceFromCurve(face, couponRate, frequency,
                maturityYears, YieldCurve::ofZeroRates(tenors, bumped));
        bumped[i] = baseRates[i] - kBump;
        double down = bond_pricer::priceFromCurve(face, couponRate, frequency,
                maturityYears, YieldCurve::ofZeroRates(tenors, bumped));
        bumped[i] = baseRates[i];
        krd[i] = (down - up) / 2;  // positive when rates-up hurts
    }
    return krd;
}

double parallelDv01(double face, double couponRate, int frequency,
                    double maturityYears, const YieldCurve& curve) {
    std::vector<double> tenors = curve.tenors();
    std::vector<double> up(tenors.size());
    std::vector<double> down(tenors.size());
    for (std::size_t i = 0; i < tenors.size(); i++) {
        double base = curve.zeroRate(tenors[i]);
        up[i] = base + kBump;
        down[i] = base - kBump;
    }
    double priceUp = bond_pricer::priceFromCurve(face, couponRate, frequency,
            maturityYears, YieldCurve::ofZeroRates(tenors, up));
    double priceDown = bond_pricer::priceFromCurve(face, couponRate, frequency,
            maturityYears, YieldCurve::ofZeroRates(tenors, down));
    return (priceDown - priceUp) / 2;
}

}  // namespace key_rate_durations
}  // namespace quantfinlib
