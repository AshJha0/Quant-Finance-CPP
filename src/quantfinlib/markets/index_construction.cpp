#include "quantfinlib/markets/index_construction.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace index_construction {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

std::size_t validatePrices(const std::vector<double>& prices) {
    if (prices.empty()) {
        throw std::invalid_argument("need at least one constituent");
    }
    for (double p : prices) {
        if (!(p > 0) || p == kInf) {
            throw std::invalid_argument("price must be positive and finite: "
                                        + std::to_string(p));
        }
    }
    return prices.size();
}

std::size_t validate(const std::vector<double>& prices,
                     const std::vector<double>& shares,
                     const std::vector<double>& floatFactors) {
    std::size_t n = validatePrices(prices);
    if (shares.size() != n || floatFactors.size() != n) {
        throw std::invalid_argument("prices/shares/floats must align");
    }
    for (std::size_t i = 0; i < n; i++) {
        if (!(shares[i] > 0) || shares[i] == kInf) {
            throw std::invalid_argument("shares must be positive and finite: "
                                        + std::to_string(shares[i]));
        }
        if (!(floatFactors[i] > 0) || floatFactors[i] > 1) {
            throw std::invalid_argument("float factor must be in (0, 1]: "
                                        + std::to_string(floatFactors[i]));
        }
    }
    return n;
}

std::vector<double> normalize(std::vector<double> w, double total) {
    for (double& x : w) {
        x /= total;
    }
    return w;
}

}  // namespace

std::vector<double> capWeights(const std::vector<double>& prices,
                               const std::vector<double>& shares,
                               const std::vector<double>& floatFactors) {
    std::size_t n = validate(prices, shares, floatFactors);
    std::vector<double> w(n);
    double total = 0;
    for (std::size_t i = 0; i < n; i++) {
        w[i] = prices[i] * shares[i] * floatFactors[i];
        total += w[i];
    }
    return normalize(std::move(w), total);
}

std::vector<double> priceWeights(const std::vector<double>& prices) {
    std::size_t n = validatePrices(prices);
    std::vector<double> w(n);
    double total = 0;
    for (std::size_t i = 0; i < n; i++) {
        w[i] = prices[i];
        total += prices[i];
    }
    return normalize(std::move(w), total);
}

std::vector<double> equalWeights(int n) {
    if (n < 1) {
        throw std::invalid_argument("need n >= 1, got " + std::to_string(n));
    }
    return std::vector<double>(static_cast<std::size_t>(n), 1.0 / n);
}

double level(const std::vector<double>& prices, const std::vector<double>& shares,
             const std::vector<double>& floatFactors, double divisor) {
    validate(prices, shares, floatFactors);
    if (!(divisor > 0) || divisor == kInf) {
        throw std::invalid_argument("divisor must be positive and finite");
    }
    double agg = 0;
    for (std::size_t i = 0; i < prices.size(); i++) {
        agg += prices[i] * shares[i] * floatFactors[i];
    }
    return agg / divisor;
}

double adjustDivisor(double oldDivisor, double oldAggregate, double newAggregate) {
    if (!(oldDivisor > 0) || !(oldAggregate > 0) || !(newAggregate > 0)) {
        throw std::invalid_argument("divisor and aggregates must be positive");
    }
    return oldDivisor * newAggregate / oldAggregate;
}

double turnover(const std::vector<double>& from, const std::vector<double>& to) {
    if (from.size() != to.size()) {
        throw std::invalid_argument("weight vectors must align");
    }
    double sum = 0;
    for (std::size_t i = 0; i < from.size(); i++) {
        if (!std::isfinite(from[i]) || !std::isfinite(to[i])) {
            throw std::invalid_argument("non-finite weight at " + std::to_string(i));
        }
        sum += std::fabs(from[i] - to[i]);
    }
    return 0.5 * sum;
}

}  // namespace index_construction
}  // namespace quantfinlib
