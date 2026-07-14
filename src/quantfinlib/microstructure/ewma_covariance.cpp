#include "quantfinlib/microstructure/ewma_covariance.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

EwmaCovariance::EwmaCovariance(int symbols, double lambda) : symbols_(symbols), lambda_(lambda) {
    if (symbols < 1 || symbols > 46'340 || lambda <= 0 || lambda >= 1) {
        throw std::invalid_argument("need symbols in [1, 46340], lambda in (0,1)");
    }
    tri_.assign(static_cast<std::size_t>(symbols) * static_cast<std::size_t>(symbols + 1) / 2, 0.0);
}

void EwmaCovariance::onReturns(const std::vector<double>& returns) {
    if (static_cast<int>(returns.size()) < symbols_) {
        throw std::invalid_argument("returns has too few entries for the basket");
    }
    for (int i = 0; i < symbols_; i++) {
        if (!std::isfinite(returns[static_cast<std::size_t>(i)])) {
            return;
        }
    }
    bool seed = samples_ == 0;
    int k = 0;
    for (int i = 0; i < symbols_; i++) {
        double ri = returns[static_cast<std::size_t>(i)];
        for (int j = 0; j <= i; j++, k++) {
            double prod = ri * returns[static_cast<std::size_t>(j)];
            tri_[static_cast<std::size_t>(k)] = seed
                    ? prod
                    : tri_[static_cast<std::size_t>(k)] + (1 - lambda_) * (prod - tri_[static_cast<std::size_t>(k)]);
        }
    }
    samples_++;
}

double EwmaCovariance::covariance(int i, int j) const {
    return i >= j ? tri_[static_cast<std::size_t>(i * (i + 1) / 2 + j)]
                  : tri_[static_cast<std::size_t>(j * (j + 1) / 2 + i)];
}

double EwmaCovariance::variance(int i) const {
    return tri_[static_cast<std::size_t>(i * (i + 1) / 2 + i)];
}

double EwmaCovariance::volatility(int i) const {
    return std::sqrt(std::max(variance(i), 0.0));
}

double EwmaCovariance::correlation(int i, int j) const {
    double denom = std::sqrt(variance(i) * variance(j));
    return denom > 0 ? math_utils::clamp(covariance(i, j) / denom, -1, 1) : 0;
}

double EwmaCovariance::portfolioVariance(const std::vector<double>& weights) const {
    requireLength(weights);
    double total = 0;
    int k = 0;
    for (int i = 0; i < symbols_; i++) {
        for (int j = 0; j <= i; j++, k++) {
            double term = weights[static_cast<std::size_t>(i)] * weights[static_cast<std::size_t>(j)]
                          * tri_[static_cast<std::size_t>(k)];
            total += i == j ? term : 2 * term;
        }
    }
    return total;
}

double EwmaCovariance::marginalContribution(const std::vector<double>& weights,
                                            std::vector<double>& out) const {
    requireLength(weights);
    requireLength(out);
    double total = portfolioVariance(weights);
    if (total <= 0) {
        for (int i = 0; i < symbols_; i++) {
            out[static_cast<std::size_t>(i)] = 0;
        }
        return total;
    }
    for (int i = 0; i < symbols_; i++) {
        double sigmaW = 0;
        for (int j = 0; j < symbols_; j++) {
            sigmaW += covariance(i, j) * weights[static_cast<std::size_t>(j)];
        }
        out[static_cast<std::size_t>(i)] = weights[static_cast<std::size_t>(i)] * sigmaW / total;
    }
    return total;
}

double EwmaCovariance::minVarianceHedgeRatio(int target, int hedge) const {
    double v = variance(hedge);
    return v > 0 ? covariance(target, hedge) / v : 0;
}

void EwmaCovariance::requireLength(const std::vector<double>& a) const {
    if (static_cast<int>(a.size()) < symbols_) {
        throw std::invalid_argument("array too short for the basket");
    }
}

}  // namespace quantfinlib
