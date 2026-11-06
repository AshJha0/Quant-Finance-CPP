#include "quantfinlib/pricing/svi_slice.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/pricing/least_squares.hpp"

namespace quantfinlib {

SviSlice::SviSlice(double a, double b, double rho, double m, double sigma)
    : a_(a), b_(b), rho_(rho), m_(m), sigma_(sigma) {
    if (!std::isfinite(a) || !std::isfinite(m) || !(b >= 0) || !std::isfinite(b)
            || !(std::fabs(rho) < 1) || !(sigma > 0) || !std::isfinite(sigma)) {
        throw std::invalid_argument("SVI requires finite a, m; b >= 0; |rho| < 1; sigma > 0");
    }
    if (a + b * sigma * std::sqrt(1 - rho * rho) < -1e-14) {
        throw std::invalid_argument("a + b sigma sqrt(1 - rho^2) must be >= 0 (negative variance)");
    }
}

double SviSlice::totalVariance(double k) const {
    double u = k - m_;
    return a_ + b_ * (rho_ * u + std::sqrt(u * u + sigma_ * sigma_));
}

double SviSlice::slope(double k) const {
    double u = k - m_;
    return b_ * (rho_ + u / std::sqrt(u * u + sigma_ * sigma_));
}

double SviSlice::curvature(double k) const {
    double u = k - m_;
    double s = std::sqrt(u * u + sigma_ * sigma_);
    return b_ * sigma_ * sigma_ / (s * s * s);
}

double SviSlice::impliedVol(double k, double t) const {
    if (!(t > 0)) {
        throw std::invalid_argument("t must be > 0");
    }
    return std::sqrt(std::max(totalVariance(k), 0.0) / t);
}

double SviSlice::densityFactor(double k) const {
    double w = totalVariance(k);
    if (!(w > 0)) {
        return -std::numeric_limits<double>::infinity();
    }
    double w1 = slope(k);
    double x = 1 - k * w1 / (2 * w);
    return x * x - 0.25 * w1 * w1 * (1 / w + 0.25) + 0.5 * curvature(k);
}

double SviSlice::minDensityFactor(double kMin, double kMax, int n) const {
    if (n < 2 || !(kMax > kMin)) {
        throw std::invalid_argument("need n >= 2 and kMax > kMin");
    }
    double lowest = std::numeric_limits<double>::infinity();
    for (int i = 0; i < n; i++) {
        lowest = std::min(lowest, densityFactor(kMin + (kMax - kMin) * i / (n - 1)));
    }
    return lowest;
}

double SviSlice::rmse(const std::vector<double>& k, const std::vector<double>& w) const {
    if (k.size() != w.size() || k.empty()) {
        throw std::invalid_argument("k and w must be non-empty and the same length");
    }
    double s = 0;
    for (std::size_t i = 0; i < k.size(); i++) {
        double e = totalVariance(k[i]) - w[i];
        s += e * e;
    }
    return std::sqrt(s / static_cast<double>(k.size()));
}

SviSlice SviSlice::calibrate(const std::vector<double>& k, const std::vector<double>& w,
                             const std::vector<double>& weights, bool butterflyPenalty) {
    std::size_t n = k.size();
    if (n < 5 || w.size() != n || (!weights.empty() && weights.size() != n)) {
        throw std::invalid_argument("need at least 5 quotes with matching lengths");
    }
    double kMin = std::numeric_limits<double>::infinity();
    double kMax = -kMin;
    double wMin = kMin;
    for (std::size_t i = 0; i < n; i++) {
        if (!std::isfinite(k[i]) || !(w[i] > 0) || !std::isfinite(w[i])
                || (!weights.empty() && !(weights[i] >= 0 && std::isfinite(weights[i])))) {
            throw std::invalid_argument("quote " + std::to_string(i) + " invalid");
        }
        kMin = std::min(kMin, k[i]);
        kMax = std::max(kMax, k[i]);
        wMin = std::min(wMin, w[i]);
    }
    std::vector<double> grid(butterflyPenalty ? 121 : 0);
    for (std::size_t i = 0; i < grid.size(); i++) {
        grid[i] = kMin - 0.5 + (kMax - kMin + 1) * static_cast<double>(i) / 120;
    }
    double lo = kMin - 1;
    double hi = kMax + 1;
    auto decode = [lo, hi](const std::vector<double>& p) {
        double rho = 0.999 * std::tanh(p[2]);
        double b = 4 / (1 + std::fabs(rho)) / (1 + std::exp(-p[1]));
        double sig = 1e-4 + std::exp(p[4]);
        double m = 0.5 * (lo + hi) + 0.5 * (hi - lo) * std::tanh(p[3]);
        double a = -b * sig * std::sqrt(1 - rho * rho) + std::exp(p[0]);
        return SviSlice(a, b, rho, m, sig);
    };
    auto res = [&](const std::vector<double>& p) {
        SviSlice s = decode(p);
        std::vector<double> r(n + grid.size());
        for (std::size_t i = 0; i < n; i++) {
            r[i] = (weights.empty() ? 1 : weights[i]) * (s.totalVariance(k[i]) - w[i]);
        }
        for (std::size_t i = 0; i < grid.size(); i++) {
            r[n + i] = BUTTERFLY_PENALTY * std::min(s.densityFactor(grid[i]), 0.0);
        }
        return r;
    };
    least_squares::Solution best{{}, std::numeric_limits<double>::infinity()};
    double scale = std::max(std::fabs(kMin), std::fabs(kMax));
    for (double rho0 : {-0.5, 0.0, 0.5}) {
        for (double sig0 : {0.05, 0.2, 0.6}) {
            for (double mFrac : {-0.5, 0.0, 0.5}) {
                double m0 = mFrac * scale;
                std::vector<double> start = {std::log(0.8 * wMin), -2,
                                             std::atanh(rho0 / 0.999),
                                             std::atanh((m0 - 0.5 * (lo + hi)) / (0.5 * (hi - lo))),
                                             std::log(sig0)};
                least_squares::Solution s = least_squares::minimise(res, start, 400);
                if (s.cost < best.cost) {
                    best = s;
                }
            }
        }
    }
    return decode(best.x);
}

}  // namespace quantfinlib
