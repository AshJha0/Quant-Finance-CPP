#include "quantfinlib/risk/gaussian_copula.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

GaussianCopula::GaussianCopula(const Matrix& correlation)
    : chol_(cholesky(correlation)), dim_(static_cast<int>(correlation.size())) {
}

void GaussianCopula::sample(JavaRandom& rnd, std::vector<double>& out,
                            std::vector<double>& scratch) const {
    requireDistinct(out, scratch);
    for (int i = 0; i < dim_; i++) {
        scratch[i] = rnd.nextGaussian();
    }
    correlate(scratch, out);
    for (int i = 0; i < dim_; i++) {
        out[i] = math_utils::normCdf(out[i]);
    }
}

void GaussianCopula::sampleT(JavaRandom& rnd, int df, std::vector<double>& out,
                             std::vector<double>& scratch) const {
    if (df < 1) {
        throw std::invalid_argument("df must be >= 1");
    }
    requireDistinct(out, scratch);
    for (int i = 0; i < dim_; i++) {
        scratch[i] = rnd.nextGaussian();
    }
    correlate(scratch, out);
    double chiSq = 0;
    for (int k = 0; k < df; k++) {
        double z = rnd.nextGaussian();
        chiSq += z * z;
    }
    double scale = std::sqrt(df / std::max(chiSq, 1e-12));
    for (int i = 0; i < dim_; i++) {
        out[i] = math_utils::tCdf(out[i] * scale, df);
    }
}

void GaussianCopula::correlate(const std::vector<double>& z,
                               std::vector<double>& out) const {
    for (int i = 0; i < dim_; i++) {
        double sum = 0;
        for (int j = 0; j <= i; j++) {
            sum += chol_[i][j] * z[j];
        }
        out[i] = sum;
    }
}

void GaussianCopula::requireDistinct(const std::vector<double>& out,
                                     const std::vector<double>& scratch) const {
    requireLength(out);
    requireLength(scratch);
    // correlate() reads scratch while writing out — aliasing them
    // silently corrupts the dependence structure from dim 3 up.
    // (Java compares references; here the vectors' identities.)
    if (&out == &scratch || out.data() == scratch.data()) {
        throw std::invalid_argument("out and scratch must be distinct arrays");
    }
}

void GaussianCopula::requireLength(const std::vector<double>& a) const {
    if (static_cast<int>(a.size()) < dim_) {
        throw std::invalid_argument("array has " + std::to_string(a.size())
                + " entries, copula has " + std::to_string(dim_));
    }
}

GaussianCopula::Matrix GaussianCopula::cholesky(const Matrix& matrix) {
    int n = static_cast<int>(matrix.size());
    // The pivot tolerance is RELATIVE to the matrix's own diagonal
    // scale: correlation matrices (diag 1) factor exactly as before,
    // and genuinely positive-definite covariances quoted in small
    // units are not rejected (a 0.5bp-daily-vol factor has variance
    // ~2.5e-9; an absolute 1e-12 floor called a valid pair of them
    // "not positive-definite").
    double maxDiag = 0;
    for (int i = 0; i < n; i++) {
        if (static_cast<int>(matrix[i].size()) != n) {
            throw std::invalid_argument("matrix must be square");
        }
        maxDiag = std::max(maxDiag, std::fabs(matrix[i][i]));
    }
    // Double.MIN_NORMAL == std::numeric_limits<double>::min().
    double pivotFloor = 1e-12 * std::max(maxDiag, std::numeric_limits<double>::min());
    Matrix l(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double sum = matrix[i][j];
            for (int k = 0; k < j; k++) {
                sum -= l[i][k] * l[j][k];
            }
            if (i == j) {
                if (!(sum > pivotFloor)) {
                    throw std::invalid_argument(
                            "matrix is not positive-definite (pivot "
                            + std::to_string(i) + " = " + std::to_string(sum) + ")");
                }
                l[i][j] = std::sqrt(sum);
            } else {
                l[i][j] = sum / l[j][j];
            }
        }
    }
    return l;
}

}  // namespace quantfinlib
