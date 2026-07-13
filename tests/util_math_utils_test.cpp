// Port of MathUtilsPairSortTest.java plus hand-pinned coverage of every
// other public MathUtils function (the Java suite exercises those through
// domain tests; the pins below are all derived by hand in the comments).
#include "quantfinlib/util/math_utils.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "qtest.hpp"

using namespace quantfinlib::math_utils;

// ------------------------------------------------------------ pairSort
// (port of MathUtilsPairSortTest)

// Java uses SplittableRandom(11); the exact stream is not reproducible in
// C++, so a seeded mt19937 supplies the same SHAPE of data: 50 trials,
// n in [1, 500], integer keys in [-25, 24] for plenty of ties. The
// assertions are identical: keys agree with the reference sort, and the
// permutation carries each original key with its index.
TEST(pairSortAgreesWithReferenceSortOnRandomData) {
    std::mt19937 rnd(11);
    for (int trial = 0; trial < 50; trial++) {
        int n = 1 + static_cast<int>(rnd() % 500);
        std::vector<double> keys(n);
        std::vector<int> values(n);
        for (int i = 0; i < n; i++) {
            keys[i] = static_cast<double>(static_cast<int>(rnd() % 50) - 25);
            values[i] = i;
        }
        std::vector<double> expectedKeys = keys;
        std::sort(expectedKeys.begin(), expectedKeys.end());
        std::vector<double> originals = keys;

        pairSort(keys, values);
        for (int i = 0; i < n; i++) {
            EXPECT_TRUE(keys[i] == expectedKeys[i]);
            // The permutation must carry each original key with its index.
            EXPECT_TRUE(keys[i] == originals[values[i]]);
        }
    }
}

TEST(pairSortAdversarialShapesAndSmallArrays) {
    // Already sorted, reversed, and constant keys (median-of-three guards),
    // then a singleton and an empty array through the insertion path.
    std::vector<std::vector<double>> shapes = {
            {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16},
            {16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1},
            {7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7},
            {3.0},
            {}};
    for (const auto& shape : shapes) {
        std::vector<double> keys = shape;
        std::vector<int> values(keys.size());
        for (std::size_t i = 0; i < values.size(); i++) {
            values[i] = static_cast<int>(i);
        }
        std::vector<double> expected = shape;
        std::sort(expected.begin(), expected.end());
        pairSort(keys, values);
        EXPECT_TRUE(keys == expected);
    }
    std::vector<double> k2(2);
    std::vector<int> v3(3);
    EXPECT_THROWS(pairSort(k2, v3));
}

// ------------------------------------------------------- moments & spread

TEST(meanVarianceStdDev) {
    std::vector<double> v = {1, 2, 3, 4};
    // mean = (1+2+3+4)/4 = 10/4 = 2.5
    EXPECT_NEAR(mean(v), 2.5, 1e-15);
    // mean over [1,3) = (2+3)/2 = 2.5
    EXPECT_NEAR(mean(v, 1, 3), 2.5, 1e-15);
    // deviations from 2.5: -1.5, -0.5, 0.5, 1.5 -> squares sum to 5,
    // sample variance = 5/3
    EXPECT_NEAR(variance(v), 5.0 / 3.0, 1e-15);
    EXPECT_NEAR(stdDev(v), std::sqrt(5.0 / 3.0), 1e-15);
    // population over [0,4): 5/4 = 1.25 -> sqrt = 1.1180339887498949
    EXPECT_NEAR(stdDevP(v, 0, 4), std::sqrt(1.25), 1e-15);
    // sample over the same range equals stdDev on the whole array
    EXPECT_NEAR(stdDevSample(v, 0, 4), std::sqrt(5.0 / 3.0), 1e-15);
    // degenerate windows return 0, matching the Java guards
    std::vector<double> one = {42};
    EXPECT_NEAR(variance(one), 0.0, 0.0);
    EXPECT_NEAR(stdDevP(one, 0, 0), 0.0, 0.0);
    EXPECT_NEAR(stdDevSample(one, 0, 1), 0.0, 0.0);
}

TEST(skewnessAndKurtosisPinned) {
    // Symmetric data has zero third central moment.
    std::vector<double> sym = {1, 2, 3};
    EXPECT_NEAR(skewness(sym), 0.0, 1e-15);
    // v = {0,0,0,4}: m = 1, deviations {-1,-1,-1,3};
    // m2 = (1+1+1+9)/4 = 3, m3 = (-1-1-1+27)/4 = 6, m4 = (1+1+1+81)/4 = 21
    // skew = 6 / 3^1.5 = 2/sqrt(3) = 1.1547005383792515
    // kurt = 21 / 9 = 2.3333333333333335
    std::vector<double> v = {0, 0, 0, 4};
    EXPECT_NEAR(skewness(v), 2.0 / std::sqrt(3.0), 1e-14);
    EXPECT_NEAR(kurtosis(v), 21.0 / 9.0, 1e-14);
    // Constant series: m2 == 0 short-circuits to 0 (no 0/0).
    std::vector<double> flat = {5, 5, 5};
    EXPECT_NEAR(skewness(flat), 0.0, 0.0);
    EXPECT_NEAR(kurtosis(flat), 0.0, 0.0);
}

// ------------------------------------------------------------ percentile

TEST(percentileLinearInterpolation) {
    std::vector<double> v = {4, 1, 3, 2};  // sorted: 1 2 3 4
    // p=0.5: idx = 0.5*3 = 1.5 -> 2*(0.5) + 3*(0.5) = 2.5
    EXPECT_NEAR(percentile(v, 0.5), 2.5, 1e-15);
    // p=0.25: idx = 0.75 -> 1*0.25 + 2*0.75 = 1.75
    EXPECT_NEAR(percentile(v, 0.25), 1.75, 1e-15);
    // endpoints hit the extremes exactly (lo == hi branch)
    EXPECT_NEAR(percentile(v, 0.0), 1.0, 0.0);
    EXPECT_NEAR(percentile(v, 1.0), 4.0, 0.0);
    // percentile must not reorder the caller's array (it copies)
    EXPECT_TRUE(v[0] == 4.0);
    // empty input -> NaN, matching Java
    std::vector<double> empty;
    EXPECT_TRUE(std::isnan(percentileSorted(empty, 0.5)));
}

// -------------------------------------------------------- linear algebra

TEST(dotMatVecQuadraticForm) {
    std::vector<double> a = {1, 2, 3}, b = {4, 5, 6};
    // 1*4 + 2*5 + 3*6 = 4 + 10 + 18 = 32
    EXPECT_NEAR(dot(a, b), 32.0, 1e-15);
    std::vector<double> short2 = {1, 2};
    EXPECT_THROWS(dot(a, short2));
    // [[1,2],[3,4]] * [5,6] = [5+12, 15+24] = [17, 39]
    Matrix m = {{1, 2}, {3, 4}};
    std::vector<double> mv = matVec(m, {5, 6});
    EXPECT_NEAR(mv[0], 17.0, 1e-15);
    EXPECT_NEAR(mv[1], 39.0, 1e-15);
    // w'Mw with w=[1,2], M=diag(2,3): 2*1 + 3*4 = 14
    Matrix diag = {{2, 0}, {0, 3}};
    EXPECT_NEAR(quadraticForm({1, 2}, diag), 14.0, 1e-15);
}

TEST(covarianceAndCorrelationPinned) {
    // a={1,2,3} (mean 2), b={2,4,6} (mean 4):
    // s = (-1)(-2) + 0 + (1)(2) = 4; sample cov = 4/2 = 2
    std::vector<double> a = {1, 2, 3}, b = {2, 4, 6};
    EXPECT_NEAR(covariance(a, b), 2.0, 1e-15);
    // stdDev(a) = 1, stdDev(b) = 2 -> corr = 2/(1*2) = 1 (perfect linear)
    EXPECT_NEAR(correlation(a, b), 1.0, 1e-15);
    // constant series short-circuits to 0 instead of dividing by 0
    std::vector<double> flat = {3, 3, 3};
    EXPECT_NEAR(correlation(a, flat), 0.0, 0.0);
    // gates: length mismatch and n < 2 both throw
    std::vector<double> two = {1, 2}, one = {1};
    EXPECT_THROWS(covariance(a, two));
    EXPECT_THROWS(covariance(one, one));
}

TEST(choleskyPinnedAndGates) {
    // A = [[4,2],[2,3]]: l00 = sqrt(4) = 2, l10 = 2/2 = 1,
    // l11 = sqrt(3 - 1*1) = sqrt(2) = 1.4142135623730951
    Matrix a = {{4, 2}, {2, 3}};
    Matrix l = cholesky(a);
    EXPECT_NEAR(l[0][0], 2.0, 1e-15);
    EXPECT_NEAR(l[1][0], 1.0, 1e-15);
    EXPECT_NEAR(l[1][1], std::sqrt(2.0), 1e-15);
    EXPECT_NEAR(l[0][1], 0.0, 0.0);
    // Borderline PSD (rank-1 all-ones correlation): pivot 1 = 1 - 1 = 0,
    // jittered to 1e-12 -> l11 = 1e-6, no throw.
    Matrix psd = {{1, 1}, {1, 1}};
    Matrix lp = cholesky(psd);
    EXPECT_NEAR(lp[1][1], 1e-6, 1e-18);
    // Genuinely indefinite (correlation 2 > 1): pivot 1 = 1 - 4 = -3,
    // far below -1e-8 * maxDiag -> throws.
    Matrix indef = {{1, 2}, {2, 1}};
    EXPECT_THROWS(cholesky(indef));
}

TEST(solveLinearPinnedAndSingularGate) {
    // 2x + y = 5, x + 3y = 10 -> y = 5 - 2x, x + 15 - 6x = 10 -> x = 1, y = 3
    Matrix a = {{2, 1}, {1, 3}};
    std::vector<double> x = solveLinear(a, {5, 10});
    EXPECT_NEAR(x[0], 1.0, 1e-12);
    EXPECT_NEAR(x[1], 3.0, 1e-12);
    // inputs untouched (the Java version clones)
    EXPECT_TRUE(a[0][0] == 2.0);
    // singular: second row is 2x the first
    Matrix sing = {{1, 2}, {2, 4}};
    EXPECT_THROWS(solveLinear(sing, {1, 2}));
}

TEST(inversePinnedAndSingularGate) {
    // [[1,2],[3,4]]: det = -2 -> inverse = [[-2, 1], [1.5, -0.5]]
    Matrix a = {{1, 2}, {3, 4}};
    Matrix inv = inverse(a);
    EXPECT_NEAR(inv[0][0], -2.0, 1e-12);
    EXPECT_NEAR(inv[0][1], 1.0, 1e-12);
    EXPECT_NEAR(inv[1][0], 1.5, 1e-12);
    EXPECT_NEAR(inv[1][1], -0.5, 1e-12);
    Matrix sing = {{1, 2}, {2, 4}};
    EXPECT_THROWS(inverse(sing));
}

// -------------------------------------------------------- distributions

TEST(normPdfAndCdfPinned) {
    // pdf(0) = 1/sqrt(2*pi) = 0.3989422804014327
    EXPECT_NEAR(normPdf(0.0), 0.3989422804014327, 1e-15);
    // A&S 26.2.17 is accurate to 7.5e-8; true Phi(0) = 0.5,
    // Phi(1.96) = 0.9750021048517795, Phi(-1.96) = 0.0249978951482205
    EXPECT_NEAR(normCdf(0.0), 0.5, 7.5e-8);
    EXPECT_NEAR(normCdf(1.96), 0.9750021048517795, 7.5e-8);
    EXPECT_NEAR(normCdf(-1.96), 0.0249978951482205, 7.5e-8);
    // symmetry is exact by construction: cdf(x) + cdf(-x) == 1
    EXPECT_NEAR(normCdf(0.7) + normCdf(-0.7), 1.0, 0.0);
}

TEST(normInvAcklamPinned) {
    // central branch: p = 0.5 -> q = 0, numerator carries the * q -> exactly 0
    EXPECT_NEAR(normInv(0.5), 0.0, 0.0);
    // true quantiles (R qnorm): 0.975 -> 1.959963984540054,
    // 0.01 -> -2.3263478740408408; Acklam |RELATIVE error| < 1.15e-9,
    // so for |z| ~ 2.3 the absolute error can reach ~2.7e-9 -> tol 5e-9.
    EXPECT_NEAR(normInv(0.975), 1.959963984540054, 5e-9);
    EXPECT_NEAR(normInv(0.01), -2.3263478740408408, 5e-9);
    // lower-tail branch (p < 0.02425): 0.02 -> -2.053748910631823
    EXPECT_NEAR(normInv(0.02), -2.053748910631823, 5e-9);
    // upper-tail branch (p > 0.97575): 0.99 -> 2.3263478740408408
    EXPECT_NEAR(normInv(0.99), 2.3263478740408408, 5e-9);
    // round trip through the ported normCdf at its own accuracy
    EXPECT_NEAR(normCdf(normInv(0.3)), 0.3, 1e-7);
    EXPECT_THROWS(normInv(0.0));
    EXPECT_THROWS(normInv(1.0));
    EXPECT_THROWS(normInv(-0.5));
}

TEST(logGammaPinned) {
    // Gamma(1) = 1 -> ln = 0; Gamma(0.5) = sqrt(pi) -> ln = 0.5723649429247001;
    // Gamma(5) = 24 -> ln 24 = 3.1780538303479458. Lanczos rel err < 2e-10.
    EXPECT_NEAR(logGamma(1.0), 0.0, 1e-9);
    EXPECT_NEAR(logGamma(0.5), 0.5723649429247001, 1e-9);
    EXPECT_NEAR(logGamma(5.0), 3.1780538303479458, 1e-9);
    // gate rejects non-positive AND NaN (the !(x > 0) idiom)
    EXPECT_THROWS(logGamma(0.0));
    EXPECT_THROWS(logGamma(-1.0));
    EXPECT_THROWS(logGamma(std::nan("")));
}

TEST(regularizedIncompleteBetaPinned) {
    // I_x(1,1) is the uniform CDF: I_0.3(1,1) = 0.3
    EXPECT_NEAR(regularizedIncompleteBeta(1, 1, 0.3), 0.3, 1e-13);
    // Beta(2,2) pdf = 6x(1-x), CDF = 3x^2 - 2x^3; at 0.5: 0.75 - 0.25 = 0.5
    EXPECT_NEAR(regularizedIncompleteBeta(2, 2, 0.5), 0.5, 1e-13);
    // at 0.25: 3/16 - 2/64 = 0.1875 - 0.03125 = 0.15625 (symmetry branch off)
    EXPECT_NEAR(regularizedIncompleteBeta(2, 2, 0.25), 0.15625, 1e-13);
    // endpoints return x exactly
    EXPECT_NEAR(regularizedIncompleteBeta(3, 4, 0.0), 0.0, 0.0);
    EXPECT_NEAR(regularizedIncompleteBeta(3, 4, 1.0), 1.0, 0.0);
    EXPECT_THROWS(regularizedIncompleteBeta(0, 1, 0.5));
    EXPECT_THROWS(regularizedIncompleteBeta(1, -1, 0.5));
    EXPECT_THROWS(regularizedIncompleteBeta(1, 1, 1.5));
}

TEST(tCdfPinned) {
    // t == 0 short-circuits to exactly 0.5
    EXPECT_NEAR(tCdf(0.0, 7), 0.5, 0.0);
    // df=1 is Cauchy: F(t) = 0.5 + atan(t)/pi; F(1) = 0.5 + 0.25 = 0.75
    EXPECT_NEAR(tCdf(1.0, 1), 0.75, 1e-12);
    EXPECT_NEAR(tCdf(-1.0, 1), 0.25, 1e-12);
    // df=2 closed form: F(t) = 0.5 + t / (2*sqrt(2 + t^2));
    // F(1) = 0.5 + 1/(2*sqrt(3)) = 0.788675134594813
    EXPECT_NEAR(tCdf(1.0, 2), 0.788675134594813, 1e-12);
    // large df approaches the normal: F(1.96, 1e6) ~ 0.975
    EXPECT_NEAR(tCdf(1.96, 1e6), 0.975, 1e-4);
    EXPECT_THROWS(tCdf(1.0, 0.0));
    EXPECT_THROWS(tCdf(1.0, -3.0));
}

// -------------------------------------------------------------- helpers

TEST(clampNanArrayDecayFactor) {
    EXPECT_NEAR(clamp(5.0, 0.0, 1.0), 1.0, 0.0);
    EXPECT_NEAR(clamp(-5.0, 0.0, 1.0), 0.0, 0.0);
    EXPECT_NEAR(clamp(0.5, 0.0, 1.0), 0.5, 0.0);
    std::vector<double> nans = nanArray(3);
    EXPECT_TRUE(nans.size() == 3);
    for (double x : nans) {
        EXPECT_TRUE(std::isnan(x));
    }
    // dt == halfLife -> exp(-ln 2) = 0.5 exactly (up to exp rounding);
    // dt == 2*halfLife -> 0.25; non-positive dt -> exactly 1.
    EXPECT_NEAR(decayFactor(100, 100), 0.5, 1e-15);
    EXPECT_NEAR(decayFactor(200, 100), 0.25, 1e-15);
    EXPECT_NEAR(decayFactor(0, 100), 1.0, 0.0);
    EXPECT_NEAR(decayFactor(-5, 100), 1.0, 0.0);
}

QTEST_MAIN()
