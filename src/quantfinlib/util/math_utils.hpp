// Port of com.quantfinlib.util.MathUtils (Java): numerical primitives
// shared across the library. All functions are allocation-light and the
// numeric algorithms (two-pass variance, Cody normCdf, Halley-refined Acklam
// normInv, Lanczos logGamma, Lentz beta continued fraction, median-of-three
// pairSort) follow the Java source, coefficients included. IllegalArgumentException
// maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {
namespace math_utils {

using Matrix = std::vector<std::vector<double>>;

double mean(const std::vector<double>& v);
double mean(const std::vector<double>& v, int from, int to);

/** Sample variance (n - 1 denominator). */
double variance(const std::vector<double>& v);

/** Sample standard deviation. */
double stdDev(const std::vector<double>& v);

/** Population standard deviation over [from, to). */
double stdDevP(const std::vector<double>& v, int from, int to);

/** Sample standard deviation over [from, to). */
double stdDevSample(const std::vector<double>& v, int from, int to);

/** Linear-interpolated percentile, p in [0, 1]. Copies and sorts the input. */
double percentile(const std::vector<double>& values, double p);

/** Percentile on an already-sorted array (no copy). */
double percentileSorted(const std::vector<double>& sorted, double p);

double dot(const std::vector<double>& a, const std::vector<double>& b);

std::vector<double> matVec(const Matrix& m, const std::vector<double>& v);

/** w' * M * w (quadratic form). */
double quadraticForm(const std::vector<double>& w, const Matrix& m);

/** Sample covariance of two equally-sized series (n >= 2). */
double covariance(const std::vector<double>& a, const std::vector<double>& b);

double correlation(const std::vector<double>& a, const std::vector<double>& b);

/**
 * Cholesky decomposition: returns lower-triangular L with A = L * L'.
 * Accepts positive semi-definite input: a zero or borderline pivot gets
 * jitter of 1e-12 * maxDiag, relative so that small-unit covariances are not
 * distorted. A pivot below -1e-8 * maxDiag means the input is indefinite and
 * throws, as do non-square and non-finite input.
 */
Matrix cholesky(const Matrix& a);

/**
 * The strict form, for callers that need a full-rank factor: any pivot at or
 * below 1e-12 * maxDiag throws. Same factorisation and scale as cholesky.
 */
Matrix choleskyStrict(const Matrix& a);

/**
 * Inverse standard normal CDF: Acklam's approximation refined by one Halley
 * step on normCdf, accurate to the last few bits of a double. Throws unless
 * p is in (0, 1).
 */
double normInv(double p);

/** Population skewness: m3 / m2^1.5. */
double skewness(const std::vector<double>& v);

/** Population kurtosis: m4 / m2^2 (3 for a normal distribution, not excess). */
double kurtosis(const std::vector<double>& v);

/**
 * Sorts keys ascending while permuting values identically. Quicksort with
 * median-of-three pivots and an insertion cutoff; NaN keys are not
 * supported (callers filter NaN before ranking/selecting).
 */
void pairSort(std::vector<double>& keys, std::vector<int>& values);

/** Standard normal density. */
double normPdf(double x);

/**
 * Standard normal CDF to full double precision: W. J. Cody's rational
 * Chebyshev approximations (Math. Comp. 23, 1969; coefficients from SPECFUN
 * ANORM, the set R's pnorm uses). The lower tail keeps about 1e-15 relative
 * accuracy down to x = -37.5; beyond that the value is subnormal, then 0.
 * NaN in gives NaN out.
 */
double normCdf(double x);

/**
 * exp(a) * normCdf(x) without the inf * 0 = NaN of the direct product, for
 * reflection-principle formulas where exp(a) overflows as normCdf(x)
 * underflows. Needs a <= x^2/2 for the result to be finite when x < -37.
 */
double expTimesNormCdf(double a, double x);

/**
 * Solves A x = b by Gaussian elimination with partial pivoting.
 * Inputs are not modified.
 */
std::vector<double> solveLinear(const Matrix& a, const std::vector<double>& b);

/** Matrix inverse by Gauss-Jordan elimination with partial pivoting. */
Matrix inverse(const Matrix& a);

double clamp(double v, double lo, double hi);

std::vector<double> nanArray(int n);

/**
 * Exponential decay factor for a half-life over an elapsed interval:
 * exp(-dt*ln2/halfLife); 1.0 for non-positive dt.
 */
double decayFactor(std::int64_t dtNanos, std::int64_t halfLifeNanos);

/** Natural log of the gamma function (Lanczos, |relative error| < 2e-10). */
double logGamma(double x);

/**
 * Regularized incomplete beta function I_x(a, b) via the continued
 * fraction (modified Lentz), switching to the symmetry
 * I_x(a,b) = 1 - I_{1-x}(b,a) where the fraction converges fastest.
 * Accurate to ~1e-13 across (0, 1).
 */
double regularizedIncompleteBeta(double a, double b, double x);

/**
 * Student-t CDF with df degrees of freedom -- exact via the regularized
 * incomplete beta (P(T <= t) = 1 - 0.5*I_{v/(v+t^2)}(v/2, 1/2) for t >= 0),
 * no normal approximation.
 */
double tCdf(double t, double df);

}  // namespace math_utils
}  // namespace quantfinlib
