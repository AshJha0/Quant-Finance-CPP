#include "quantfinlib/risk/stress_tester.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/risk/var_engine.hpp"

namespace quantfinlib {
namespace stress_tester {

namespace {

void requireSameLength(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size() || a.size() < 1) {
        throw std::invalid_argument("exposures and shocks must align");
    }
}

void requireFinite(const std::vector<double>& a, const std::string& name) {
    for (double x : a) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument(name + " must be finite (one NaN "
                    + "exposure would print NaN for every scenario in the report)");
        }
    }
}

}  // namespace

double scenarioPnl(const std::vector<double>& exposures, const std::vector<double>& shocks) {
    requireSameLength(exposures, shocks);
    requireFinite(exposures, "exposures");
    requireFinite(shocks, "shocks");
    double pnl = 0;
    for (std::size_t i = 0; i < exposures.size(); i++) {
        pnl += exposures[i] * shocks[i];
    }
    return pnl;
}

double scenarioPnl(const std::vector<double>& exposures, const Matrix& gamma,
                   const std::vector<double>& shocks) {
    double pnl = scenarioPnl(exposures, shocks);
    std::size_t n = exposures.size();
    if (gamma.size() != n) {
        throw std::invalid_argument("gamma must match exposures");
    }
    for (const auto& row : gamma) {
        requireFinite(row, "gamma");
    }
    for (std::size_t i = 0; i < n; i++) {
        for (std::size_t j = 0; j < n; j++) {
            pnl += 0.5 * shocks[i] * gamma[i][j] * shocks[j];
        }
    }
    return pnl;
}

std::vector<double> sensitivityLadder(const std::vector<double>& exposures, int factor,
                                      double range, int steps) {
    if (factor < 0 || factor >= static_cast<int>(exposures.size()) || steps < 2
            || !(range > 0) || range == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("invalid ladder spec");
    }
    requireFinite(exposures, "exposures");
    std::vector<double> pnl(steps + 1);
    for (int s = 0; s <= steps; s++) {
        double shock = -range + 2 * range * s / steps;
        pnl[s] = exposures[factor] * shock;
    }
    return pnl;
}

std::vector<double> sensitivityLadder(const std::vector<double>& exposures,
                                      const Matrix& gamma, int factor,
                                      double range, int steps) {
    if (gamma.size() != exposures.size()) {
        throw std::invalid_argument("gamma must match exposures");
    }
    for (const auto& row : gamma) {
        requireFinite(row, "gamma");
    }
    std::vector<double> pnl = sensitivityLadder(exposures, factor, range, steps);
    for (int s = 0; s <= steps; s++) {
        double shock = -range + 2 * range * s / steps;
        pnl[s] += 0.5 * gamma[factor][factor] * shock * shock;
    }
    return pnl;
}

ReverseStress reverseStress(const std::vector<double>& exposures, const Matrix& covariance,
                            double targetLoss) {
    if (!(targetLoss > 0) || targetLoss == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("targetLoss must be positive and finite");
    }
    double sigmaP = var_engine::portfolioStdev(exposures, covariance);
    if (!(sigmaP > 0)) {
        throw std::invalid_argument("the book carries no factor risk — no finite move loses "
                + std::to_string(targetLoss));
    }
    std::size_t n = exposures.size();
    std::vector<double> sigmaDelta(n, 0.0);
    for (std::size_t i = 0; i < n; i++) {
        for (std::size_t j = 0; j < n; j++) {
            sigmaDelta[i] += covariance[i][j] * exposures[j];
        }
    }
    double scale = -targetLoss / (sigmaP * sigmaP);  // d'Sd = sigma_P^2
    std::vector<double> shocks(n);
    for (std::size_t i = 0; i < n; i++) {
        shocks[i] = scale * sigmaDelta[i];
    }
    return {std::move(shocks), targetLoss / sigmaP};
}

std::vector<double> blackMonday1987() {
    return {-0.20, -0.0050, 0.02, -0.05, 0.20};
}

std::vector<double> lehman2008() {
    return {-0.09, -0.0040, 0.04, -0.07, 0.16};
}

std::vector<double> covidMarch2020() {
    return {-0.12, -0.0030, 0.05, -0.15, 0.25};
}

}  // namespace stress_tester
}  // namespace quantfinlib
