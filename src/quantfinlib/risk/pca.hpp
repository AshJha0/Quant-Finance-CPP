// Port of com.quantfinlib.risk.Pca (Java): principal component analysis
// of a covariance matrix via the cyclic Jacobi eigenvalue algorithm.
// The matrix is normalized to unit scale before Jacobi (entries near
// 1e155 would overflow the squared-norm accumulators; eigenvalues scale
// back linearly); convergence thresholds are RELATIVE to the matrix's
// own norm, the per-element skip threshold is consistent with the total
// off-diagonal stop, eigenvalues clip at zero (PSD noise), and 100
// sweeps without convergence throws std::runtime_error. Asymmetric
// input beyond floating-point noise throws std::invalid_argument.
#pragma once

#include <vector>

namespace quantfinlib {

class Pca {
public:
    using Matrix = std::vector<std::vector<double>>;

    /** Decomposes a symmetric covariance matrix (n x n, row-major square). */
    explicit Pca(const Matrix& covariance);

    /** Variance carried by component c (descending order). */
    double eigenvalue(int c) const { return eigenvalues_[c]; }

    /** Unit loading of factor f on component c. */
    double loading(int c, int f) const { return eigenvectors_[c][f]; }

    /** Fraction of total variance the first k components explain. */
    double explainedVariance(int k) const;

    int size() const { return static_cast<int>(eigenvalues_.size()); }

private:
    std::vector<double> eigenvalues_;  // descending
    Matrix eigenvectors_;              // [component][factor], unit length
    double totalVariance_;
};

}  // namespace quantfinlib
