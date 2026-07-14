// Port of com.quantfinlib.risk.GaussianCopula (Java): Gaussian and
// Student-t copula samplers — dependence separated from marginals.
// Both samplers emit correlated UNIFORMS; the Gaussian copula has no
// tail dependence, the t copula with few df has strong symmetric tail
// dependence and converges to the Gaussian as df -> infinity. The
// t-copula uniforms use the EXACT t-CDF (math_utils::tCdf), not a
// moment-matched normal. Correlation matrices Cholesky-factor at
// construction and a non-positive-definite input throws
// std::invalid_argument there, not as NaN samples later. Deterministic
// per seed via the caller-owned JavaRandom.
#pragma once

#include <vector>

#include "quantfinlib/risk/java_random.hpp"

namespace quantfinlib {

class GaussianCopula {
public:
    using Matrix = std::vector<std::vector<double>>;

    /** @param correlation symmetric positive-definite correlation matrix */
    explicit GaussianCopula(const Matrix& correlation);

    /**
     * One Gaussian-copula draw: out[i] are correlated uniforms in (0, 1).
     * scratch must be a distinct array of the same length (kept separate
     * so the sampler allocates nothing per draw).
     */
    void sample(JavaRandom& rnd, std::vector<double>& out,
                std::vector<double>& scratch) const;

    /**
     * One Student-t-copula draw with df degrees of freedom: the same
     * correlated Gaussians divided by a shared sqrt(chi^2/df) — the
     * SHARED shock creates the tail dependence. Uniforms via the exact
     * t-CDF.
     */
    void sampleT(JavaRandom& rnd, int df, std::vector<double>& out,
                 std::vector<double>& scratch) const;

    int dimension() const { return dim_; }

    /**
     * Cholesky factorization; fails loudly (std::invalid_argument) on a
     * non-positive-definite input. Pivot tolerance is RELATIVE to the
     * matrix's own diagonal scale so covariances quoted in small units
     * are not rejected.
     */
    static Matrix cholesky(const Matrix& matrix);

private:
    Matrix chol_;  // lower-triangular Cholesky factor
    int dim_;

    void correlate(const std::vector<double>& z, std::vector<double>& out) const;
    void requireDistinct(const std::vector<double>& out,
                         const std::vector<double>& scratch) const;
    void requireLength(const std::vector<double>& a) const;
};

}  // namespace quantfinlib
