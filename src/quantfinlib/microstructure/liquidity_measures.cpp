#include "quantfinlib/microstructure/liquidity_measures.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace liquidity_measures {

namespace {
double square(double x) { return x * x; }

void requirePositiveFinite(const std::vector<double>& a) {
    double inf = std::numeric_limits<double>::infinity();
    for (double x : a) {
        if (!(x > 0) || x == inf) {
            throw std::invalid_argument("prices must be positive and finite");
        }
    }
}
}  // namespace

double rollSpread(const std::vector<double>& prices) {
    if (prices.size() < 3) {
        throw std::invalid_argument("need >= 3 prices");
    }
    requirePositiveFinite(prices);
    int n = static_cast<int>(prices.size()) - 1;
    std::vector<double> d(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        d[static_cast<std::size_t>(i)] = prices[static_cast<std::size_t>(i + 1)]
                - prices[static_cast<std::size_t>(i)];
    }
    double mean = math_utils::mean(d);
    double cov = 0;
    for (int i = 1; i < n; i++) {
        cov += (d[static_cast<std::size_t>(i)] - mean) * (d[static_cast<std::size_t>(i - 1)] - mean);
    }
    cov /= (n - 1);
    return cov < 0 ? 2 * std::sqrt(-cov) : std::numeric_limits<double>::quiet_NaN();
}

double corwinSchultzSpread(double high1, double low1, double high2, double low2) {
    requirePositiveFinite({high1, low1, high2, low2});
    if (high1 < low1 || high2 < low2) {
        throw std::invalid_argument("high must be >= low");
    }
    double b = square(std::log(high1 / low1)) + square(std::log(high2 / low2));
    double gamma = square(std::log(std::max(high1, high2) / std::min(low1, low2)));
    double k = 3 - 2 * std::sqrt(2.0);
    double alpha = (std::sqrt(2 * b) - std::sqrt(b)) / k - std::sqrt(gamma / k);
    double spread = 2 * (std::exp(alpha) - 1) / (1 + std::exp(alpha));
    return std::max(0.0, spread);
}

double amihudIlliquidity(const std::vector<double>& returns, const std::vector<double>& dollarVolumes) {
    if (returns.size() != dollarVolumes.size() || returns.empty()) {
        throw std::invalid_argument("need aligned, non-empty arrays");
    }
    double inf = std::numeric_limits<double>::infinity();
    double sum = 0;
    for (std::size_t i = 0; i < returns.size(); i++) {
        if (!std::isfinite(returns[i])) {
            throw std::invalid_argument("returns must be finite");
        }
        if (!(dollarVolumes[i] > 0) || dollarVolumes[i] == inf) {
            throw std::invalid_argument(
                    "dollarVolumes must be positive and finite (zero volume is a data gap, not "
                    "infinite illiquidity)");
        }
        sum += std::abs(returns[i]) / dollarVolumes[i];
    }
    return sum / static_cast<double>(returns.size());
}

}  // namespace liquidity_measures
}  // namespace quantfinlib
