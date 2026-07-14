#include "quantfinlib/rates/short_rate_models.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace short_rate_models {

namespace {

void requirePositive(double x, const char* name) {
    if (!(x > 0) || x == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

void requireNonNegative(double x, const char* name) {
    if (!(x >= 0) || x == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument(std::string(name) + " must be >= 0 and finite");
    }
}

}  // namespace

// ---------------------------------------------------------------- Vasicek

double vasicekBond(double shortRate, double a, double b, double sigma,
                   double maturityYears) {
    requirePositive(a, "a");
    requireNonNegative(sigma, "sigma");
    requirePositive(maturityYears, "maturityYears");
    double bT = (1 - std::exp(-a * maturityYears)) / a;
    double logA = (bT - maturityYears) * (b - sigma * sigma / (2 * a * a))
            - sigma * sigma * bT * bT / (4 * a);
    return std::exp(logA - bT * shortRate);
}

double vasicekYield(double shortRate, double a, double b, double sigma,
                    double maturityYears) {
    return -std::log(vasicekBond(shortRate, a, b, sigma, maturityYears)) / maturityYears;
}

double vasicekStep(double shortRate, double a, double b, double sigma,
                   double dtYears, double gaussian) {
    double decay = std::exp(-a * dtYears);
    double stdev = sigma * std::sqrt((1 - decay * decay) / (2 * a));
    return b + (shortRate - b) * decay + stdev * gaussian;
}

// -------------------------------------------------------------------- CIR

double cirBond(double shortRate, double a, double b, double sigma,
               double maturityYears) {
    requirePositive(a, "a");
    requirePositive(sigma, "sigma");
    requirePositive(maturityYears, "maturityYears");
    requireNonNegative(shortRate, "shortRate");
    double h = std::sqrt(a * a + 2 * sigma * sigma);
    double expHt = std::exp(h * maturityYears);
    double denom = 2 * h + (a + h) * (expHt - 1);
    double bT = 2 * (expHt - 1) / denom;
    double aT = std::pow(2 * h * std::exp((a + h) * maturityYears / 2) / denom,
                         2 * a * b / (sigma * sigma));
    return aT * std::exp(-bT * shortRate);
}

double cirFeller(double a, double b, double sigma) {
    return 2 * a * b / (sigma * sigma);
}

double cirStep(double shortRate, double a, double b, double sigma,
               double dtYears, double gaussian) {
    double rPlus = std::max(shortRate, 0.0);
    return shortRate + a * (b - rPlus) * dtYears
            + sigma * std::sqrt(rPlus * dtYears) * gaussian;
}

// ------------------------------------- Hull-White (fitted to a market curve)

double hullWhiteBond(const YieldCurve& curve, double tYears, double maturityYears,
                     double shortRate, double a, double sigma) {
    requirePositive(a, "a");
    requireNonNegative(sigma, "sigma");
    requirePositive(maturityYears, "maturityYears");
    requireNonNegative(tYears, "tYears");
    double bT = (1 - std::exp(-a * maturityYears)) / a;
    double pT = curve.discountFactor(tYears + maturityYears);
    double pt = tYears == 0 ? 1.0 : curve.discountFactor(tYears);
    double fwd = instantaneousForward(curve, tYears);
    double variance = sigma * sigma * (1 - std::exp(-2 * a * tYears)) / (4 * a);
    return (pT / pt) * std::exp(bT * (fwd - shortRate) - variance * bT * bT);
}

double instantaneousForward(const YieldCurve& curve, double tYears) {
    requireNonNegative(tYears, "tYears");
    double h = 1.0 / 365;
    if (tYears < h) {
        // A centered window would clamp into a one-sided average of
        // f near (t+h)/2, not t — biased on a steep money-market end.
        // Second-order one-sided stencil of g(t) = -ln P(t) instead
        // (exact whenever g is locally quadratic).
        double g0 = -std::log(curve.discountFactor(tYears));
        double g1 = -std::log(curve.discountFactor(tYears + h));
        double g2 = -std::log(curve.discountFactor(tYears + 2 * h));
        return (-3 * g0 + 4 * g1 - g2) / (2 * h);
    }
    double lo = tYears - h;
    double hi = tYears + h;
    return -(std::log(curve.discountFactor(hi)) - std::log(curve.discountFactor(lo)))
            / (hi - lo);
}

}  // namespace short_rate_models
}  // namespace quantfinlib
