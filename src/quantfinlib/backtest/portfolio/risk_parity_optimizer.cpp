#include "quantfinlib/backtest/portfolio/risk_parity_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace risk_parity_optimizer {

Allocation equalRiskContribution(const std::vector<double>& expectedReturns,
                                 const math_utils::Matrix& covariance) {
    std::size_t n = covariance.size();
    if (expectedReturns.size() != n) {
        throw std::invalid_argument("expectedReturns (" + std::to_string(expectedReturns.size())
                + ") must align with covariance (" + std::to_string(n) + ")");
    }
    for (std::size_t i = 0; i < n; i++) {
        if (covariance[i].size() != n) {
            throw std::invalid_argument("covariance row " + std::to_string(i)
                                        + " is not length " + std::to_string(n));
        }
        // A zero-variance asset has no risk to contribute: the ERC fixed
        // point does not exist and the multiplicative update thrashes on
        // it. Refuse rather than spin.
        if (!(covariance[i][i] > 0)) {
            throw std::invalid_argument("asset " + std::to_string(i)
                    + " has non-positive variance: " + std::to_string(covariance[i][i]));
        }
    }
    std::vector<double> w(n, 1.0 / static_cast<double>(n));

    bool converged = false;
    for (int iter = 0; iter < 10000 && !converged; iter++) {
        std::vector<double> marginal = math_utils::matVec(covariance, w);
        double portVar = math_utils::dot(w, marginal);
        double target = portVar / static_cast<double>(n);
        double maxDeviation = 0;
        double sum = 0;
        for (std::size_t i = 0; i < n; i++) {
            double rc = w[i] * marginal[i];
            maxDeviation = std::max(maxDeviation, std::fabs(rc - target) / portVar);
            // Multiplicative update pulls each contribution toward the target.
            w[i] *= std::pow(target / std::max(rc, 1e-16), 0.5);
            sum += w[i];
        }
        for (std::size_t i = 0; i < n; i++) {
            w[i] /= sum;
        }
        converged = maxDeviation < 1e-10;
    }
    if (!converged) {
        // Refuse rather than hand back a stalled iterate as if it were
        // the ERC solution.
        throw std::runtime_error(
                "equal-risk-contribution fixed point did not converge in 10000 iterations");
    }
    double ret = math_utils::dot(expectedReturns, w);
    double vol = std::sqrt(math_utils::quadraticForm(w, covariance));
    Allocation a;
    a.weights = std::move(w);
    a.expectedReturn = ret;
    a.volatility = vol;
    a.sharpe = vol == 0 ? 0 : ret / vol;
    return a;
}

std::vector<double> riskContributions(const std::vector<double>& w,
                                      const math_utils::Matrix& covariance) {
    std::vector<double> marginal = math_utils::matVec(covariance, w);
    double portVar = math_utils::dot(w, marginal);
    std::vector<double> rc(w.size());
    for (std::size_t i = 0; i < w.size(); i++) {
        rc[i] = portVar == 0 ? 0 : w[i] * marginal[i] / portVar;
    }
    return rc;
}

}  // namespace risk_parity_optimizer
}  // namespace quantfinlib
