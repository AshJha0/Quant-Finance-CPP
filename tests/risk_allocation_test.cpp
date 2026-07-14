// Port of com.quantfinlib.risk.RiskAllocationTest (Java): Component VaR
// (Euler allocation) and Ledoit-Wolf shrinkage, pinned on hand-derivable
// identities. correlatedReturns replays the exact java.util.Random
// streams (seeds 7, 42, 11) via JavaRandom, so the Java tolerances
// carry unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/component_var.hpp"
#include "quantfinlib/risk/covariance_shrinkage.hpp"
#include "quantfinlib/risk/java_random.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace component_var = quantfinlib::component_var;
namespace covariance_shrinkage = quantfinlib::covariance_shrinkage;
namespace math_utils = quantfinlib::math_utils;
using quantfinlib::JavaRandom;
using Matrix = component_var::Matrix;

TEST(componentsSumExactlyToPortfolioVarOnHandCase) {
    // w = {100, 200}, Sigma = [[0.04, 0.01], [0.01, 0.09]]:
    // Sigma w = {6, 19}, w' Sigma w = 4400, sigma = sqrt(4400).
    std::vector<double> w = {100, 200};
    Matrix cov = {{0.04, 0.01}, {0.01, 0.09}};
    auto a = component_var::allocate(w, cov, 0.99);

    double sigma = std::sqrt(4400.0);
    double z = math_utils::normInv(0.99);
    EXPECT_NEAR(z * sigma, a.portfolioVar, 1e-9);
    EXPECT_NEAR(100 * z * 6 / sigma, a.components[0], 1e-9);
    EXPECT_NEAR(200 * z * 19 / sigma, a.components[1], 1e-9);
    // The Euler identity: components sum EXACTLY to the total.
    EXPECT_NEAR(a.portfolioVar, a.components[0] + a.components[1], 1e-9);
    EXPECT_NEAR(z * 6 / sigma, a.marginals[0], 1e-12);
}

TEST(uncorrelatedEqualPositionsSplitEvenly) {
    std::vector<double> w = {1, 1};
    Matrix cov = {{0.01, 0}, {0, 0.01}};
    auto a = component_var::allocate(w, cov, 0.95);
    EXPECT_NEAR(a.components[0], a.components[1], 1e-12);
    EXPECT_NEAR(a.portfolioVar / 2, a.components[0], 1e-12);
}

TEST(hedgeHasNegativeComponentAndClosingItRaisesVar) {
    // Perfectly correlated long 100 / short 50: the short is a hedge.
    std::vector<double> w = {100, -50};
    Matrix cov = {{0.04, 0.04}, {0.04, 0.04}};
    auto a = component_var::allocate(w, cov, 0.99);
    EXPECT_TRUE(a.components[1] < 0);  // a hedge owns NEGATIVE risk
    EXPECT_NEAR(a.portfolioVar, a.components[0] + a.components[1], 1e-9);

    // Incremental VaR of the hedge: closing it moves sigma 10 -> 20
    // (sqrt(w'Sw) = 0.2*|100+(-50)| = 10 vs 0.2*100 = 20), so the
    // "risk removed" is negative.
    double incr = component_var::incremental(w, cov, 0.99, 1);
    double z = math_utils::normInv(0.99);
    EXPECT_NEAR(z * 10 - z * 20, incr, 1e-9);
    EXPECT_TRUE(incr < 0);
}

TEST(componentVarGates) {
    Matrix cov = {{0.04, 0.01}, {0.01, 0.09}};
    EXPECT_THROWS(component_var::allocate({1}, cov, 0.99));
    EXPECT_THROWS(component_var::allocate({1, 1}, cov, 0.5));
    EXPECT_THROWS(component_var::allocate({1, 1}, cov, 1.0));
    EXPECT_THROWS(component_var::allocate({0, 0}, cov, 0.99));  // flat book
    EXPECT_THROWS(component_var::allocate({1, std::nan("")}, cov, 0.99));
    EXPECT_THROWS(component_var::incremental({1, 1}, cov, 0.99, 2));
}

// -------------------------------------------------------- CovarianceShrinkage

namespace {

// Two correlated + one independent series, deterministic — same stream
// as the Java test's new Random(seed).
Matrix correlatedReturns(int t, long long seed) {
    JavaRandom rnd(seed);
    Matrix r(t, std::vector<double>(3));
    for (int i = 0; i < t; i++) {
        double common = rnd.nextGaussian();
        r[i][0] = 0.01 * common;
        r[i][1] = 0.01 * (0.8 * common + 0.6 * rnd.nextGaussian());
        r[i][2] = 0.02 * rnd.nextGaussian();
    }
    return r;
}

}  // namespace

TEST(shrinkagePreservesAverageVarianceExactly) {
    // tr(Sigma*) = delta*n*mu + (1-delta)*tr(S) = n*mu — an identity,
    // so the average variance survives shrinkage to 1e-12.
    auto res = covariance_shrinkage::ledoitWolf(correlatedReturns(60, 7));
    const auto& m = res.matrix;
    double trace = m[0][0] + m[1][1] + m[2][2];
    EXPECT_NEAR(res.target, trace / 3, 1e-12);
    EXPECT_TRUE(res.intensity >= 0 && res.intensity <= 1);
    for (int a = 0; a < 3; a++) {
        for (int b = 0; b < 3; b++) {
            EXPECT_NEAR(m[a][b], m[b][a], 0.0);  // must stay symmetric
        }
    }
}

TEST(moreDataMeansLessShrinkage) {
    double d20 = covariance_shrinkage::ledoitWolf(correlatedReturns(20, 42)).intensity;
    double d2000 = covariance_shrinkage::ledoitWolf(correlatedReturns(2000, 42)).intensity;
    EXPECT_TRUE(d2000 < d20);  // T = 2000 must trust the sample more
    EXPECT_TRUE(d20 > 0.05);   // 20 observations of 3 assets is noisy
}

TEST(shrinkagePullsOffDiagonalsTowardZero) {
    auto res = covariance_shrinkage::ledoitWolf(correlatedReturns(40, 11));
    // The target has zero off-diagonals, so |shrunk| <= |sample| there.
    // Recover the sample entry from the convex combination.
    double delta = res.intensity;
    double shrunk = res.matrix[0][1];
    double sample = shrunk / (1 - delta);  // target contribution is 0
    EXPECT_TRUE(std::fabs(shrunk) < std::fabs(sample));
}

TEST(shrinkageGates) {
    EXPECT_THROWS(covariance_shrinkage::ledoitWolf(
            Matrix(1, std::vector<double>(2, 0.0))));
    Matrix ragged = {{0.1, 0.2}, {0.1}};
    EXPECT_THROWS(covariance_shrinkage::ledoitWolf(ragged));
    Matrix nan(5, std::vector<double>(2, 0.0));
    nan[2][1] = std::nan("");
    EXPECT_THROWS(covariance_shrinkage::ledoitWolf(nan));
}

QTEST_MAIN()
