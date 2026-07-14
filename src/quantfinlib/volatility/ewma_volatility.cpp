#include "quantfinlib/volatility/ewma_volatility.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

EwmaVolatility::EwmaVolatility(double lambda) : lambda_(lambda) {
    if (lambda <= 0 || lambda >= 1) {
        throw std::invalid_argument("lambda must be in (0,1): " + std::to_string(lambda));
    }
}

EwmaVolatility EwmaVolatility::riskMetrics() {
    return EwmaVolatility(0.94);
}

std::vector<double> EwmaVolatility::variances(const std::vector<double>& returns) const {
    if (returns.size() < 2) {
        throw std::invalid_argument("need at least 2 returns");
    }
    std::vector<double> h(returns.size());
    h[0] = math_utils::variance(returns);   // unconditional seed
    for (std::size_t t = 1; t < returns.size(); t++) {
        h[t] = lambda_ * h[t - 1] + (1 - lambda_) * returns[t - 1] * returns[t - 1];
    }
    return h;
}

double EwmaVolatility::latestVol(const std::vector<double>& returns) const {
    std::vector<double> h = variances(returns);
    double last = h.back();
    double next = lambda_ * last + (1 - lambda_) * returns.back() * returns.back();
    return std::sqrt(next);
}

double EwmaVolatility::annualizedVol(const std::vector<double>& returns,
                                     int periodsPerYear) const {
    return latestVol(returns) * std::sqrt(static_cast<double>(periodsPerYear));
}

}  // namespace quantfinlib
