#include "quantfinlib/rates/swap_pricer.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace swap_pricer {

namespace {

void requireTenor(int tenorYears) {
    if (tenorYears < 1) {
        throw std::invalid_argument("tenorYears must be >= 1, got "
                                    + std::to_string(tenorYears));
    }
}

}  // namespace

double annuity(const YieldCurve& curve, int tenorYears) {
    requireTenor(tenorYears);
    double a = 0;
    for (int i = 1; i <= tenorYears; i++) {
        a += curve.discountFactor(i);
    }
    return a;
}

double parRate(const YieldCurve& curve, int tenorYears) {
    return (1 - curve.discountFactor(tenorYears)) / annuity(curve, tenorYears);
}

double payerPv(const YieldCurve& curve, int tenorYears, double fixedRate) {
    if (!std::isfinite(fixedRate)) {
        throw std::invalid_argument("fixedRate must be finite");
    }
    return annuity(curve, tenorYears) * (parRate(curve, tenorYears) - fixedRate);
}

double dv01(const YieldCurve& curve, int tenorYears, double fixedRate) {
    double base = payerPv(curve, tenorYears, fixedRate);
    double bumped = payerPv(parallelBump(curve, 1e-4), tenorYears, fixedRate);
    return bumped - base;
}

YieldCurve parallelBump(const YieldCurve& curve, double bump) {
    std::vector<double> tenors = curve.tenors();
    std::vector<double> z(tenors.size());
    for (std::size_t i = 0; i < tenors.size(); i++) {
        z[i] = curve.zeroRate(tenors[i]) + bump;
    }
    return YieldCurve::ofZeroRates(tenors, z);
}

}  // namespace swap_pricer
}  // namespace quantfinlib
