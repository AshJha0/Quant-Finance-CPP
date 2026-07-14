#include "quantfinlib/backtest/portfolio/black_litterman.hpp"

#include <stdexcept>

namespace quantfinlib {
namespace black_litterman {

std::vector<double> impliedEquilibriumReturns(double riskAversion,
                                              const math_utils::Matrix& covariance,
                                              const std::vector<double>& marketWeights) {
    std::vector<double> pi = math_utils::matVec(covariance, marketWeights);
    for (std::size_t i = 0; i < pi.size(); i++) {
        pi[i] *= riskAversion;
    }
    return pi;
}

std::vector<double> posteriorReturns(double tau, const math_utils::Matrix& covariance,
                                     const std::vector<double>& equilibriumReturns,
                                     const math_utils::Matrix& p,
                                     const std::vector<double>& q,
                                     const std::vector<double>& omegaDiag) {
    std::size_t n = equilibriumReturns.size();
    std::size_t views = p.size();
    if (views == 0) {
        return equilibriumReturns;
    }
    if (q.size() != views || omegaDiag.size() != views) {
        throw std::invalid_argument("q and omega must have one entry per view");
    }
    // (tau*Sigma)^-1
    math_utils::Matrix tauSigma(n, std::vector<double>(n));
    for (std::size_t i = 0; i < n; i++) {
        for (std::size_t j = 0; j < n; j++) {
            tauSigma[i][j] = tau * covariance[i][j];
        }
    }
    math_utils::Matrix priorPrecision = math_utils::inverse(tauSigma);

    // A = (tau*Sigma)^-1 + P' Omega^-1 P ;  b = (tau*Sigma)^-1 Pi + P' Omega^-1 Q
    math_utils::Matrix a = priorPrecision;
    std::vector<double> b = math_utils::matVec(priorPrecision, equilibriumReturns);
    for (std::size_t v = 0; v < views; v++) {
        if (omegaDiag[v] <= 0) {
            throw std::invalid_argument("view variance must be positive");
        }
        double invOmega = 1 / omegaDiag[v];
        for (std::size_t i = 0; i < n; i++) {
            b[i] += p[v][i] * invOmega * q[v];
            for (std::size_t j = 0; j < n; j++) {
                a[i][j] += p[v][i] * invOmega * p[v][j];
            }
        }
    }
    return math_utils::solveLinear(a, b);
}

}  // namespace black_litterman
}  // namespace quantfinlib
