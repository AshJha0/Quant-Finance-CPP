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
    return math_utils::choleskyStrict(matrix);
}

}  // namespace quantfinlib
