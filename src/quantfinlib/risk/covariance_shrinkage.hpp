// Port of com.quantfinlib.risk.CovarianceShrinkage (Java): Ledoit-Wolf
// (2004) shrinkage toward the scaled identity mu*I, with intensity
// delta = b^2/d^2 chosen from the data (d^2 = ||S - mu*I||_F^2 distance
// to target, b^2 = estimated sampling noise, clamped to d^2). Sample
// covariance S uses the POPULATION (/T) convention, as Ledoit-Wolf do.
// Result is positive-definite for delta > 0.
#pragma once

#include <vector>

namespace quantfinlib {
namespace covariance_shrinkage {

using Matrix = std::vector<std::vector<double>>;

/**
 * matrix:    the shrunk covariance Sigma*
 * intensity: delta in [0, 1] — how far toward the target
 * target:    mu, the average sample variance (the target's diagonal)
 */
struct Result {
    Matrix matrix;
    double intensity;
    double target;
};

/**
 * @param returns T x N: returns[t][j] = period-t return of asset j;
 *                T >= 2, N >= 1, rectangular, finite
 */
Result ledoitWolf(const Matrix& returns);

/** Convenience: the shrunk matrix only. */
Matrix shrink(const Matrix& returns);

}  // namespace covariance_shrinkage
}  // namespace quantfinlib
