#include "quantfinlib/risk/component_var.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace component_var {

Allocation allocate(const std::vector<double>& weights, const Matrix& covariance,
                    double confidence) {
    int n = static_cast<int>(weights.size());
    if (n == 0 || static_cast<int>(covariance.size()) != n) {
        throw std::invalid_argument("weights (" + std::to_string(n)
                + ") and covariance (" + std::to_string(covariance.size())
                + ") must align");
    }
    if (!(confidence > 0.5) || !(confidence < 1)) {
        throw std::invalid_argument("confidence must be in (0.5, 1), got "
                + std::to_string(confidence));
    }
    for (int i = 0; i < n; i++) {
        if (!std::isfinite(weights[i])) {
            throw std::invalid_argument("non-finite weight at " + std::to_string(i));
        }
        if (static_cast<int>(covariance[i].size()) != n) {
            throw std::invalid_argument("covariance row " + std::to_string(i)
                    + " is not length " + std::to_string(n));
        }
        for (int j = 0; j < n; j++) {
            if (!std::isfinite(covariance[i][j])) {
                throw std::invalid_argument("non-finite covariance at "
                        + std::to_string(i) + "," + std::to_string(j));
            }
        }
    }
    std::vector<double> sw = math_utils::matVec(covariance, weights);
    double variance = math_utils::dot(weights, sw);
    if (!(variance > 0)) {
        throw std::invalid_argument(
                "portfolio variance must be > 0 (flat or perfectly hedged book), got "
                + std::to_string(variance));
    }
    double sigma = std::sqrt(variance);
    double z = math_utils::normInv(confidence);
    std::vector<double> components(n);
    std::vector<double> marginals(n);
    for (int i = 0; i < n; i++) {
        marginals[i] = z * sw[i] / sigma;
        components[i] = weights[i] * marginals[i];
    }
    return {z * sigma, std::move(components), std::move(marginals)};
}

double incremental(const std::vector<double>& weights, const Matrix& covariance,
                   double confidence, int i) {
    if (i < 0 || i >= static_cast<int>(weights.size())) {
        throw std::invalid_argument("position index " + std::to_string(i)
                + " out of range");
    }
    double full = allocate(weights, covariance, confidence).portfolioVar;
    std::vector<double> without = weights;
    without[i] = 0;
    double sw = math_utils::quadraticForm(without, covariance);
    // A book that is flat without this position has zero remaining VaR.
    double rest = sw > 0 ? math_utils::normInv(confidence) * std::sqrt(sw) : 0;
    return full - rest;
}

}  // namespace component_var
}  // namespace quantfinlib
