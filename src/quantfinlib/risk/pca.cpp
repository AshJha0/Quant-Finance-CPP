#include "quantfinlib/risk/pca.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace quantfinlib {

namespace {

using Matrix = Pca::Matrix;

void rotate(Matrix& m, Matrix& v, int p, int q, double c, double s, int n) {
    for (int i = 0; i < n; i++) {
        double mip = m[i][p];
        double miq = m[i][q];
        m[i][p] = c * mip - s * miq;
        m[i][q] = s * mip + c * miq;
    }
    for (int i = 0; i < n; i++) {
        double mpi = m[p][i];
        double mqi = m[q][i];
        m[p][i] = c * mpi - s * mqi;
        m[q][i] = s * mpi + c * mqi;
        double vip = v[i][p];
        double viq = v[i][q];
        v[i][p] = c * vip - s * viq;
        v[i][q] = s * vip + c * viq;
    }
}

void jacobi(Matrix& m, Matrix& v) {
    int n = static_cast<int>(m.size());
    // Convergence must be RELATIVE to the matrix's own scale — the
    // caller chooses the units (return fractions vs currency^2, ~1e16
    // apart), so absolute thresholds either never trigger (burning all
    // sweeps on a converged matrix) or trigger too early.
    double normSq = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            normSq += m[i][j] * m[i][j];
        }
    }
    if (normSq == 0) {
        return;  // the zero matrix is already diagonal
    }
    double offTol = 1e-24 * normSq;
    // The per-element skip threshold must be CONSISTENT with the total
    // off-diagonal stop: if every element sits below skipTol, their
    // summed squares must already satisfy off < offTol — otherwise all
    // rotations get skipped while the stop never fires, and the sweep
    // loop spins to the non-convergence throw on a converged matrix.
    double pairs = n > 1 ? n * (n - 1) / 2.0 : 1;
    double skipTol = std::sqrt(offTol / pairs);
    for (int sweep = 0; sweep < 100; sweep++) {
        double off = 0;
        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                off += m[p][q] * m[p][q];
            }
        }
        if (off < offTol) {
            return;
        }
        for (int p = 0; p < n; p++) {
            for (int q = p + 1; q < n; q++) {
                if (std::fabs(m[p][q]) < skipTol) {
                    continue;
                }
                double theta = (m[q][q] - m[p][p]) / (2 * m[p][q]);
                double signum = theta > 0 ? 1.0 : (theta < 0 ? -1.0 : 0.0);
                double t = signum / (std::fabs(theta) + std::sqrt(theta * theta + 1));
                if (theta == 0) {
                    t = 1;
                }
                double c = 1 / std::sqrt(t * t + 1);
                double s = t * c;
                rotate(m, v, p, q, c, s, n);
            }
        }
    }
    // Jacobi converges quadratically — a symmetric matrix that has not
    // converged in 100 sweeps is a signal, not a diagonal to report.
    // Java IllegalStateException -> std::runtime_error.
    throw std::runtime_error("Jacobi failed to converge in 100 sweeps");
}

Matrix identity(int n) {
    Matrix id(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; i++) {
        id[i][i] = 1;
    }
    return id;
}

}  // namespace

Pca::Pca(const Matrix& covariance) {
    int n = static_cast<int>(covariance.size());
    if (n < 1) {
        throw std::invalid_argument("empty matrix");
    }
    Matrix m(n, std::vector<double>(n));
    for (int i = 0; i < n; i++) {
        if (static_cast<int>(covariance[i].size()) != n) {
            throw std::invalid_argument("matrix must be square");
        }
        for (int j = 0; j < n; j++) {
            if (!std::isfinite(covariance[i][j])) {
                throw std::invalid_argument("non-finite covariance entry");
            }
            if (std::fabs(covariance[i][j] - covariance[j][i])
                    > 1e-9 * (1 + std::fabs(covariance[i][j]))) {
                throw std::invalid_argument("matrix must be symmetric");
            }
            m[i][j] = covariance[i][j];
        }
    }
    // Normalize to unit scale before Jacobi: entries near 1e155 would
    // overflow the squared-norm accumulators to Infinity, and entries
    // near 1e-155 would underflow them to zero — either way corrupting
    // the convergence thresholds. Eigenvalues scale back linearly.
    double scale = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            scale = std::max(scale, std::fabs(m[i][j]));
        }
    }
    if (scale > 0) {
        for (int i = 0; i < n; i++) {
            for (int j = 0; j < n; j++) {
                m[i][j] /= scale;
            }
        }
    } else {
        scale = 1;  // zero matrix: nothing to scale
    }
    Matrix v = identity(n);
    jacobi(m, v);

    // Extract diagonal eigenvalues + columns, sort descending.
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::vector<double> eig(n);
    for (int i = 0; i < n; i++) {
        eig[i] = m[i][i];
    }
    std::stable_sort(order.begin(), order.end(),
                     [&eig](int x, int y) { return eig[y] < eig[x]; });
    eigenvalues_.assign(n, 0.0);
    eigenvectors_.assign(n, std::vector<double>(n, 0.0));
    double total = 0;
    for (int c = 0; c < n; c++) {
        int src = order[c];
        eigenvalues_[c] = std::max(0.0, eig[src]) * scale;  // PSD clip (see class doc)
        total += eigenvalues_[c];
        for (int f = 0; f < n; f++) {
            eigenvectors_[c][f] = v[f][src];
        }
    }
    totalVariance_ = total;
}

double Pca::explainedVariance(int k) const {
    if (k < 1 || k > static_cast<int>(eigenvalues_.size())) {
        throw std::invalid_argument("k out of range");
    }
    if (totalVariance_ <= 0) {
        return 0;
    }
    double sum = 0;
    for (int c = 0; c < k; c++) {
        sum += eigenvalues_[c];
    }
    return sum / totalVariance_;
}

}  // namespace quantfinlib
