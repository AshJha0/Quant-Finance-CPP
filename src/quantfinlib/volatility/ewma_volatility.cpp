#include "quantfinlib/volatility/ewma_volatility.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

EwmaVolatility::EwmaVolatility(double lambda) : lambda_(lambda) {
    if (!(lambda > 0 && lambda < 1)) {
        throw std::invalid_argument("lambda must be in (0,1): " + std::to_string(lambda));
    }
}

EwmaVolatility EwmaVolatility::riskMetrics() {
    return EwmaVolatility(0.94);
}

namespace {

void requireReturns(const std::vector<double>& returns) {
    if (returns.size() < 2) {
        throw std::invalid_argument("need at least 2 returns");
    }
    for (double r : returns) {
        if (!std::isfinite(r)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
}

}  // namespace

std::vector<double> EwmaVolatility::variances(const std::vector<double>& returns) const {
    requireReturns(returns);
    std::vector<double> h(returns.size());
    double raw = 0;   // sum (1-lambda) lambda^{t-1-k} r_k^2, uncorrected
    double mass = 0;  // 1 - lambda^t
    for (std::size_t t = 1; t < returns.size(); t++) {
        raw = lambda_ * raw + (1 - lambda_) * returns[t - 1] * returns[t - 1];
        mass = lambda_ * mass + (1 - lambda_);
        h[t] = raw / mass;
    }
    h[0] = h[1];
    return h;
}

std::vector<double> EwmaVolatility::variances(const std::vector<double>& returns,
                                              double seedVariance) const {
    requireReturns(returns);
    if (!(seedVariance > 0) || !std::isfinite(seedVariance)) {
        throw std::invalid_argument("seedVariance must be > 0 and finite");
    }
    std::vector<double> h(returns.size());
    h[0] = seedVariance;
    for (std::size_t t = 1; t < returns.size(); t++) {
        h[t] = lambda_ * h[t - 1] + (1 - lambda_) * returns[t - 1] * returns[t - 1];
    }
    return h;
}

double EwmaVolatility::latestVol(const std::vector<double>& returns) const {
    std::vector<double> h = variances(returns);
    double mass = 1 - std::pow(lambda_, static_cast<double>(returns.size() - 1));
    double r = returns.back();
    double next = (lambda_ * mass * h.back() + (1 - lambda_) * r * r)
            / (lambda_ * mass + (1 - lambda_));
    return std::sqrt(next);
}

double EwmaVolatility::annualizedVol(const std::vector<double>& returns,
                                     int periodsPerYear) const {
    return latestVol(returns) * std::sqrt(static_cast<double>(periodsPerYear));
}

}  // namespace quantfinlib
