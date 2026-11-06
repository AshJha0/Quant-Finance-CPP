// Port of com.quantfinlib.pricing.SviSlice (Java): Gatheral's raw SVI total
// variance w(k) = a + b*(rho*(k-m) + sqrt((k-m)^2 + sigma^2)) at log-moneyness
// k, with the Gatheral-Jacquier (2014) density factor
// g(k) = (1 - k w'/(2w))^2 - (w'^2/4)(1/w + 1/4) + w''/2; g < 0 is butterfly
// arbitrage. Calibration is multi-start Levenberg-Marquardt, by default with
// a penalty on negative g over a grid wider than the quotes.
#pragma once

#include <vector>

namespace quantfinlib {

class SviSlice {
public:
    static constexpr double BUTTERFLY_PENALTY = 100;

    /** Throws unless b >= 0, |rho| < 1, sigma > 0 and the minimum variance is >= 0. */
    SviSlice(double a, double b, double rho, double m, double sigma);

    double a() const { return a_; }
    double b() const { return b_; }
    double rho() const { return rho_; }
    double m() const { return m_; }
    double sigma() const { return sigma_; }

    double totalVariance(double k) const;
    double slope(double k) const;
    double curvature(double k) const;
    double impliedVol(double k, double t) const;
    double densityFactor(double k) const;
    double minDensityFactor(double kMin, double kMax, int n) const;
    double rmse(const std::vector<double>& k, const std::vector<double>& w) const;

    /** Fits to total variances w at k; weights may be empty. Needs 5 quotes. */
    static SviSlice calibrate(const std::vector<double>& k, const std::vector<double>& w,
                              const std::vector<double>& weights = {},
                              bool butterflyPenalty = true);

private:
    double a_, b_, rho_, m_, sigma_;
};

}  // namespace quantfinlib
