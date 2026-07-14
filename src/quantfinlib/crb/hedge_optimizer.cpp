#include "quantfinlib/crb/hedge_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace hedge_optimizer {

namespace {

// Generous: near-collinear instruments (0.999-correlated futures) contract
// slowly under Gauss-Seidel, and a silent max-iteration exit would return
// a plausible-looking, grossly unconverged hedge.
constexpr int kMaxIterations = 20'000;
constexpr double kRelativeTolerance = 1e-10;

void requireFinite(const std::vector<double>& a, const std::string& name) {
    for (double x : a) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument(name + " must be finite");
        }
    }
}

double matVecRow(const Matrix& cov, const std::vector<double>& e, int row) {
    double s = 0;
    for (std::size_t g = 0; g < e.size(); g++) {
        s += cov[row][g] * e[g];
    }
    return s;
}

}  // namespace

std::vector<double> hedge(const std::vector<double>& exposures, const Matrix& covariance,
                          const Matrix& loadings, const std::vector<double>& costPerUnit,
                          double costWeight) {
    int n = static_cast<int>(exposures.size());
    int m = static_cast<int>(costPerUnit.size());
    requireFinite(exposures, "exposures");
    requireFinite(costPerUnit, "costPerUnit");
    if (!(costWeight >= 0) || costWeight == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("costWeight must be >= 0 and finite");
    }
    if (static_cast<int>(covariance.size()) != n || static_cast<int>(loadings.size()) != n) {
        throw std::invalid_argument("covariance and loadings must have n factor rows");
    }
    for (const auto& row : covariance) {
        if (static_cast<int>(row.size()) != n) {
            throw std::invalid_argument("covariance must be square");
        }
        requireFinite(row, "covariance");
    }
    for (const auto& row : loadings) {
        if (static_cast<int>(row.size()) != m) {
            throw std::invalid_argument("each loadings row needs m columns");
        }
        requireFinite(row, "loadings");
    }
    for (double c : costPerUnit) {
        if (c < 0) {
            throw std::invalid_argument("costs must be >= 0");
        }
    }

    // Precompute per-instrument quadratic terms: a_i = L_i' S L_i and the
    // cross terms gram[i][j] = L_i' S L_j, plus d_i = L_i' S e.
    Matrix sigmaL(n, std::vector<double>(m, 0.0));
    for (int f = 0; f < n; f++) {
        for (int i = 0; i < m; i++) {
            double s = 0;
            for (int g = 0; g < n; g++) {
                s += covariance[f][g] * loadings[g][i];
            }
            sigmaL[f][i] = s;
        }
    }
    Matrix gram(m, std::vector<double>(m, 0.0));
    std::vector<double> d(m, 0.0);
    for (int i = 0; i < m; i++) {
        for (int j = 0; j < m; j++) {
            double s = 0;
            for (int f = 0; f < n; f++) {
                s += loadings[f][i] * sigmaL[f][j];
            }
            gram[i][j] = s;
        }
        double s = 0;
        for (int f = 0; f < n; f++) {
            s += loadings[f][i] * matVecRow(covariance, exposures, f);
        }
        d[i] = s;
    }

    std::vector<double> h(m, 0.0);
    bool converged = false;
    for (int iter = 0; iter < kMaxIterations && !converged; iter++) {
        double maxDelta = 0;
        double maxH = 1;
        for (int i = 0; i < m; i++) {
            double a = gram[i][i];
            if (a < 0) {
                throw std::invalid_argument("covariance is not PSD: instrument "
                                            + std::to_string(i) + " has L'SL = "
                                            + std::to_string(a));
            }
            if (a == 0) {
                continue;  // instrument carries no risk: leave at 0
            }
            double b = d[i];
            for (int j = 0; j < m; j++) {
                if (j != i) {
                    b += gram[i][j] * h[j];
                }
            }
            double threshold = costWeight * costPerUnit[i] / 2;
            double next;
            if (b > threshold) {
                next = -(b - threshold) / a;
            } else if (b < -threshold) {
                next = -(b + threshold) / a;
            } else {
                next = 0;
            }
            double delta = std::fabs(next - h[i]);
            if (delta > maxDelta) {
                maxDelta = delta;
            }
            h[i] = next;
            double mag = std::fabs(next);
            if (mag > maxH) {
                maxH = mag;
            }
        }
        converged = maxDelta < kRelativeTolerance * maxH;
    }
    if (!converged) {
        throw std::runtime_error(
            "coordinate descent failed to converge in " + std::to_string(kMaxIterations)
            + " iterations -- hedge instruments may be near-collinear");
    }
    return h;
}

std::vector<double> residual(const std::vector<double>& exposures, const Matrix& loadings,
                             const std::vector<double>& h) {
    int n = static_cast<int>(exposures.size());
    std::vector<double> out(n);
    for (int f = 0; f < n; f++) {
        double s = exposures[f];
        for (std::size_t i = 0; i < h.size(); i++) {
            s += loadings[f][i] * h[i];
        }
        out[f] = s;
    }
    return out;
}

double risk(const std::vector<double>& exposures, const Matrix& covariance) {
    return std::sqrt(std::max(0.0, math_utils::quadraticForm(exposures, covariance)));
}

}  // namespace hedge_optimizer
}  // namespace quantfinlib
