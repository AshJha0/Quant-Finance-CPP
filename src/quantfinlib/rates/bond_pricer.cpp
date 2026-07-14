#include "quantfinlib/rates/bond_pricer.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace bond_pricer {

namespace {

int periods(int frequency, double yearsToMaturity) {
    if (frequency < 1 || yearsToMaturity <= 0) {
        throw std::invalid_argument("need positive frequency and maturity");
    }
    return static_cast<int>(std::lround(yearsToMaturity * frequency));
}

}  // namespace

double priceFromYield(double face, double couponRate, int frequency,
                      double yearsToMaturity, double yield) {
    int n = periods(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double y = yield / frequency;
    double price = 0;
    for (int i = 1; i <= n; i++) {
        price += coupon / std::pow(1 + y, i);
    }
    return price + face / std::pow(1 + y, n);
}

double priceFromCurve(double face, double couponRate, int frequency,
                      double yearsToMaturity, const YieldCurve& curve) {
    int n = periods(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double price = 0;
    for (int i = 1; i <= n; i++) {
        double t = static_cast<double>(i) / frequency;
        price += coupon * curve.discountFactor(t);
    }
    return price + face * curve.discountFactor(static_cast<double>(n) / frequency);
}

double yieldToMaturity(double price, double face, double couponRate,
                       int frequency, double yearsToMaturity) {
    double lo = -0.9, hi = 10;
    // Bracket check: a price outside [PV(10), PV(-0.9)] has no yield in
    // the search range, and bisection would silently converge to an
    // endpoint and hand back -89.99% or 1000% as if it were a market
    // yield. Refuse loudly instead.
    double maxPrice = priceFromYield(face, couponRate, frequency, yearsToMaturity, lo);
    double minPrice = priceFromYield(face, couponRate, frequency, yearsToMaturity, hi);
    if (!(price >= minPrice) || !(price <= maxPrice)) {
        throw std::invalid_argument("price " + std::to_string(price)
                + " has no yield in [-90%, 1000%] (attainable price range ["
                + std::to_string(minPrice) + ", " + std::to_string(maxPrice) + "])");
    }
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2;
        if (priceFromYield(face, couponRate, frequency, yearsToMaturity, mid) > price) {
            lo = mid;   // price too high -> yield higher
        } else {
            hi = mid;
        }
    }
    return (lo + hi) / 2;
}

double macaulayDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield) {
    int n = periods(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double y = yield / frequency;
    double weighted = 0, price = 0;
    for (int i = 1; i <= n; i++) {
        double t = static_cast<double>(i) / frequency;
        double cf = coupon + (i == n ? face : 0);
        double pv = cf / std::pow(1 + y, i);
        weighted += t * pv;
        price += pv;
    }
    return weighted / price;
}

double modifiedDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield) {
    return macaulayDuration(face, couponRate, frequency, yearsToMaturity, yield)
            / (1 + yield / frequency);
}

double convexity(double face, double couponRate, int frequency,
                 double yearsToMaturity, double yield) {
    double h = 1e-4;
    double p0 = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield);
    double up = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield + h);
    double dn = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield - h);
    return (up + dn - 2 * p0) / (p0 * h * h);
}

double dv01(double face, double couponRate, int frequency,
            double yearsToMaturity, double yield) {
    double price = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield);
    return modifiedDuration(face, couponRate, frequency, yearsToMaturity, yield)
            * price * 1e-4;
}

}  // namespace bond_pricer
}  // namespace quantfinlib
