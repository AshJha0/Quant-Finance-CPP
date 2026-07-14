// Port of com.quantfinlib.risk.MarketRiskTest (Java): dependence, PCA,
// copulas, EVT, stress, FRTB, PLAT and the review-regression pins. The
// GJR-GARCH sections belong to the volatility domain's tests and are
// not duplicated here. Random-driven cases replay the EXACT
// java.util.Random streams (seeds 42, 7, 11, 9) via JavaRandom, so the
// Java tolerances carry unchanged.
#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/dependence.hpp"
#include "quantfinlib/risk/extreme_value_theory.hpp"
#include "quantfinlib/risk/frtb_es.hpp"
#include "quantfinlib/risk/gaussian_copula.hpp"
#include "quantfinlib/risk/java_random.hpp"
#include "quantfinlib/risk/pca.hpp"
#include "quantfinlib/risk/pnl_attribution.hpp"
#include "quantfinlib/risk/risk_metrics.hpp"
#include "quantfinlib/risk/stress_tester.hpp"
#include "quantfinlib/risk/var_engine.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::GaussianCopula;
using quantfinlib::JavaRandom;
using quantfinlib::Pca;
namespace dependence = quantfinlib::dependence;
namespace evt = quantfinlib::extreme_value_theory;
namespace frtb_es = quantfinlib::frtb_es;
namespace math_utils = quantfinlib::math_utils;
namespace pnl_attribution = quantfinlib::pnl_attribution;
namespace risk_metrics = quantfinlib::risk_metrics;
namespace stress_tester = quantfinlib::stress_tester;
namespace var_engine = quantfinlib::var_engine;
using Matrix = std::vector<std::vector<double>>;

// ------------------------------------------------------------------
// Dependence
// ------------------------------------------------------------------

TEST(rankCorrelationsSurviveWhatWrecksPearson) {
    JavaRandom rnd(42);
    std::vector<double> x(500);
    std::vector<double> y(500);
    for (std::size_t i = 0; i < x.size(); i++) {
        x[i] = rnd.nextGaussian();
        y[i] = std::exp(3 * x[i]);  // monotone but violently nonlinear
    }
    // A monotone transform is invisible to ranks.
    EXPECT_NEAR(1.0, dependence::spearman(x, y), 1e-9);
    EXPECT_NEAR(1.0, dependence::kendallTau(x, y), 1e-9);
    // While Pearson is dragged around by the convexity.
    EXPECT_TRUE(risk_metrics::correlation(x, y) < 0.75);
    // Kendall on a hand-checkable case: (2 concordant - 1 discordant) / 3 pairs.
    double tau = dependence::kendallTau({1, 2, 3}, {1, 3, 2});
    EXPECT_NEAR(1.0 / 3, tau, 1e-12);
    // The elliptical bridge at the corners and center.
    EXPECT_NEAR(0, dependence::pearsonFromKendall(0), 1e-12);
    EXPECT_NEAR(1, dependence::pearsonFromKendall(1), 1e-12);
    EXPECT_NEAR(std::sin(std::numbers::pi * 0.25),
                dependence::pearsonFromKendall(0.5), 1e-12);
}

// ------------------------------------------------------------------
// PCA
// ------------------------------------------------------------------

TEST(pcaRecoversAnalyticEigenstructure) {
    // [[2,1],[1,2]]: eigenvalues 3 and 1, eigenvectors (1,1)/sqrt2, (1,-1)/sqrt2.
    Pca pca(Matrix{{2, 1}, {1, 2}});
    EXPECT_NEAR(3, pca.eigenvalue(0), 1e-9);
    EXPECT_NEAR(1, pca.eigenvalue(1), 1e-9);
    EXPECT_NEAR(0.75, pca.explainedVariance(1), 1e-9);  // 3 of 4 total variance
    double ratio = pca.loading(0, 0) / pca.loading(0, 1);
    EXPECT_NEAR(1, ratio, 1e-6);  // first component loads both factors equally

    // A one-factor 'curve': all tenors driven by one level shock.
    Matrix level(3, std::vector<double>(3, 1e-4));  // rank-1: pure level
    Pca curve(level);
    // One real factor -> one component explains everything.
    EXPECT_NEAR(1.0, curve.explainedVariance(1), 1e-9);
    EXPECT_THROWS(Pca(Matrix{{1, 0.5}, {0.2, 1}}));  // asymmetric

    // Currency-unit covariance (~1e8 entries): convergence thresholds
    // are relative to the matrix's scale, so this decomposes exactly
    // like its rate-fraction twin above.
    Pca big(Matrix{{2e8, 1e8}, {1e8, 2e8}});
    EXPECT_NEAR(3e8, big.eigenvalue(0), 1e-2);
    EXPECT_NEAR(1e8, big.eigenvalue(1), 1e-2);
}

// ------------------------------------------------------------------
// Copulas
// ------------------------------------------------------------------

namespace {

int jointTail(const GaussianCopula& copula, JavaRandom& rnd, bool t, int df) {
    std::vector<double> out(2);
    std::vector<double> scratch(2);
    int joint = 0;
    for (int i = 0; i < 20'000; i++) {
        if (t) {
            copula.sampleT(rnd, df, out, scratch);
        } else {
            copula.sample(rnd, out, scratch);
        }
        if (out[0] < 0.02 && out[1] < 0.02) {
            joint++;
        }
    }
    return joint;
}

}  // namespace

TEST(copulaSamplesCarryTheRequestedDependence) {
    Matrix corr = {{1, 0.7}, {0.7, 1}};
    GaussianCopula copula(corr);
    JavaRandom rnd(7);
    int n = 20'000;
    std::vector<double> u1(n);
    std::vector<double> u2(n);
    std::vector<double> out(2);
    std::vector<double> scratch(2);
    for (int i = 0; i < n; i++) {
        copula.sample(rnd, out, scratch);
        u1[i] = out[0];
        u2[i] = out[1];
        EXPECT_TRUE(out[0] > 0 && out[0] < 1 && out[1] > 0 && out[1] < 1);
    }
    // Spearman of a Gaussian copula: (6/pi)*asin(rho/2) ~ 0.683 at rho 0.7.
    double expected = 6 / std::numbers::pi * std::asin(0.7 / 2);
    EXPECT_NEAR(expected, dependence::spearman(u1, u2), 0.03);

    // Tail dependence: joint-extreme frequency, t(3) vs Gaussian.
    int jointGauss = jointTail(copula, rnd, false, 0);
    int jointT = jointTail(copula, rnd, true, 3);
    EXPECT_TRUE(jointT > jointGauss * 1.3);  // t-copula extremes cluster

    // A fake correlation matrix fails at the Cholesky, loudly.
    EXPECT_THROWS(GaussianCopula(Matrix{{1, 1.2}, {1.2, 1}}));
}

TEST(tCopulaUniformsAreActuallyUniformInTheTails) {
    // The t-CDF against closed forms: df = 1 is Cauchy, df = 2 is
    // algebraic, df -> infinity is the normal.
    EXPECT_NEAR(0.5 + std::atan(0.5) / std::numbers::pi, math_utils::tCdf(0.5, 1), 1e-12);
    EXPECT_NEAR(0.5 * (1 + 1 / std::sqrt(3.0)), math_utils::tCdf(1, 2), 1e-12);
    EXPECT_NEAR(0.5, math_utils::tCdf(0, 7), 1e-12);
    EXPECT_NEAR(math_utils::normCdf(-1.5), math_utils::tCdf(-1.5, 1'000'000), 1e-4);

    // Marginal tail mass: P(U < 0.05) must BE 5% — the moment-matched
    // normal approximation this replaced put ~3.3% there at df = 3,
    // distorting exactly the quantiles a tail sampler exists for.
    GaussianCopula copula(Matrix{{1, 0.5}, {0.5, 1}});
    JavaRandom rnd(11);
    std::vector<double> out(2);
    std::vector<double> scratch(2);
    int below = 0;
    int n = 40'000;
    for (int i = 0; i < n; i++) {
        copula.sampleT(rnd, 3, out, scratch);
        if (out[0] < 0.05) {
            below++;
        }
    }
    // Exact t-CDF => uniform marginals, tails included.
    EXPECT_NEAR(0.05, below / static_cast<double>(n), 0.005);
}

// ------------------------------------------------------------------
// EVT
// ------------------------------------------------------------------

TEST(evtRecoversAPlantedParetoTailAndRefusesInfiniteMeans) {
    // Pareto(alpha = 4) exceedances: GPD shape = 1/4 exactly.
    JavaRandom rnd(42);
    std::vector<double> losses(20'000);
    for (std::size_t i = 0; i < losses.size(); i++) {
        losses[i] = std::pow(rnd.nextDouble(), -1.0 / 4);  // Pareto >= 1
    }
    auto fit = evt::fitPot(losses, 0.90);
    EXPECT_NEAR(0.25, fit.shape, 0.06);  // the planted tail index
    // The extrapolated 99.9% must exceed the fitted-threshold quantile
    // and the sample's own empirical 99.9% should be in its vicinity.
    double var999 = fit.var(0.999);
    std::vector<double> sorted = losses;
    std::sort(sorted.begin(), sorted.end());
    double empirical = sorted[static_cast<int>(0.999 * sorted.size())];
    EXPECT_NEAR(empirical, var999, 0.25 * empirical);
    EXPECT_TRUE(fit.expectedShortfall(0.999) > var999);

    // A tail with no mean refuses to average itself.
    evt::GpdFit infinite{1, 1.2, 0.5, 100, 1000};
    EXPECT_THROWS(infinite.expectedShortfall(0.999));
    // Inside-sample quantiles belong to plain historical VaR.
    EXPECT_THROWS(fit.var(0.5));
}

TEST(evtIgnoresTiesAtTheThreshold) {
    // Discretized losses: ten observations sit EXACTLY at the threshold
    // value (P&L snapped to ticks does this) — counted as y = 0
    // exceedances they would deflate both PWMs and bias the shape.
    std::vector<double> losses(100);
    for (int i = 0; i < 45; i++) {
        losses[i] = 1 + i;  // bulk below the threshold
    }
    for (int i = 45; i < 55; i++) {
        losses[i] = 50.0;   // tie block spanning the 0.5 quantile
    }
    for (int i = 55; i < 100; i++) {
        double p = (i - 55 + 0.5) / 45;  // GPD(xi=0.25, beta=2) quantiles above
        losses[i] = 50 + 2 / 0.25 * (std::pow(1 - p, -0.25) - 1);
    }
    auto fit = evt::fitPot(losses, 0.5);
    EXPECT_TRUE(fit.exceedances == 45);  // ties AT the threshold are not exceedances
    EXPECT_NEAR(0.25, fit.shape, 0.15);  // the planted tail survives ties
    EXPECT_TRUE(fit.var(0.99) > 50);
}

// ------------------------------------------------------------------
// Stress testing
// ------------------------------------------------------------------

TEST(stressScenariosAndReverseStressCloseTheLoop) {
    std::vector<double> exposures = {2'000'000, -1'000'000, 500'000, 0, 300'000};
    double pnl = stress_tester::scenarioPnl(exposures, stress_tester::lehman2008());
    EXPECT_TRUE(pnl < 0);  // a long-equity short-rates book bleeds in Lehman week

    // Delta-gamma: short gamma makes the same shock worse.
    Matrix gamma(5, std::vector<double>(5, 0.0));
    gamma[0][0] = -1e7;
    EXPECT_TRUE(stress_tester::scenarioPnl(exposures, gamma, stress_tester::lehman2008())
                < pnl);

    // The ladder is linear in the shock for a delta book.
    std::vector<double> ladder = stress_tester::sensitivityLadder(exposures, 0, 0.10, 4);
    EXPECT_NEAR(-200'000, ladder[0], 1e-6);  // -10% x 2M
    EXPECT_NEAR(0, ladder[2], 1e-6);
    EXPECT_NEAR(200'000, ladder[4], 1e-6);

    // The delta-gamma ladder shows the curvature the linear one hides:
    // short gamma costs 0.5 * 1e7 * 0.01 = 50k at BOTH +/-10% rungs.
    std::vector<double> dgLadder =
            stress_tester::sensitivityLadder(exposures, gamma, 0, 0.10, 4);
    EXPECT_NEAR(-250'000, dgLadder[0], 1e-6);  // the down rung is WORSE short gamma
    EXPECT_NEAR(0, dgLadder[2], 1e-6);
    EXPECT_NEAR(150'000, dgLadder[4], 1e-6);

    // Reverse stress: the returned shock loses EXACTLY the target.
    Matrix cov = {{4e-4, 1e-4}, {1e-4, 2.25e-4}};
    std::vector<double> expo2 = {1'000'000, -500'000};
    auto reverse = stress_tester::reverseStress(expo2, cov, 50'000);
    EXPECT_NEAR(-50'000, stress_tester::scenarioPnl(expo2, reverse.shocks), 1e-6);
    // And reports its own implausibility.
    EXPECT_NEAR(50'000 / var_engine::portfolioStdev(expo2, cov),
                reverse.mahalanobisSigmas, 1e-9);
    EXPECT_THROWS(stress_tester::reverseStress({0, 0}, cov, 50'000));
}

// ------------------------------------------------------------------
// FRTB ES, traffic light, PLAT
// ------------------------------------------------------------------

TEST(frtbArithmeticMatchesTheRegulationsFormulas) {
    // Cascade, hand-computed: ES {10, 8, 5} at horizons {10, 20, 60}:
    // sqrt(10^2 + (8*1)^2 + (5*2)^2) = sqrt(100 + 64 + 100) = sqrt(264).
    double es = frtb_es::liquidityHorizonEs({10, 8, 5}, {10, 20, 60});
    EXPECT_NEAR(std::sqrt(264.0), es, 1e-12);

    // The stressed ratio scales capital up, never down.
    EXPECT_NEAR(150, frtb_es::stressCalibratedEs(100, 30, 20), 1e-12);  // ratio 1.5
    // A calm stressed period floors at 1, never discounts.
    EXPECT_NEAR(100, frtb_es::stressCalibratedEs(100, 10, 20), 1e-12);

    // ES 97.5 of a known tail: losses 1..1000; VaR = 975th value, ES =
    // mean of 975..1000 (26 values) = 987.5 exactly.
    std::vector<double> losses(1000);
    for (int i = 0; i < 1000; i++) {
        losses[i] = i + 1;
    }
    EXPECT_NEAR(987.5, frtb_es::es975(losses), 1e-9);

    EXPECT_TRUE(frtb_es::trafficLightOf(4) == frtb_es::TrafficLight::GREEN);
    EXPECT_TRUE(frtb_es::trafficLightOf(5) == frtb_es::TrafficLight::AMBER);
    EXPECT_TRUE(frtb_es::trafficLightOf(9) == frtb_es::TrafficLight::AMBER);
    EXPECT_TRUE(frtb_es::trafficLightOf(10) == frtb_es::TrafficLight::RED);
    EXPECT_THROWS(frtb_es::liquidityHorizonEs({10}, {20}));
}

TEST(platPassesAFaithfulModelAndFlagsAMissingFactor) {
    JavaRandom rnd(9);
    int days = 250;
    std::vector<double> hpl(days);
    std::vector<double> rtplGood(days);
    std::vector<double> rtplBad(days);
    for (int d = 0; d < days; d++) {
        double factor1 = rnd.nextGaussian();
        double factor2 = rnd.nextGaussian();
        hpl[d] = 100 * factor1 + 80 * factor2;
        rtplGood[d] = 100 * factor1 + 80 * factor2 + 3 * rnd.nextGaussian();
        rtplBad[d] = 100 * factor1;  // the model MISSES factor 2
    }
    auto good = pnl_attribution::test(hpl, rtplGood);
    EXPECT_TRUE(good.zone == pnl_attribution::Zone::GREEN);  // a faithful model passes
    auto bad = pnl_attribution::test(hpl, rtplBad);
    // A missing risk factor cannot pass PLAT.
    EXPECT_TRUE(bad.zone != pnl_attribution::Zone::GREEN);
    // Identical series: the degenerate perfect score.
    auto perfect = pnl_attribution::test(hpl, hpl);
    EXPECT_NEAR(1.0, perfect.spearmanCorrelation, 1e-9);
    EXPECT_NEAR(0.0, perfect.ksStatistic, 1e-9);
}

// ------------------------------------------------------------------
// Review regressions
// ------------------------------------------------------------------

TEST(reviewRegressionsStayFixed) {
    // One NaN P&L day used to HANG ksStatistic in an infinite loop in
    // the Java original (NaN == NaN is false, so neither pointer
    // advanced) — throws now (gated pre-sort here, see pnl_attribution).
    std::vector<double> good(20);
    std::vector<double> bad(20);
    for (int i = 0; i < 20; i++) {
        good[i] = i;
        bad[i] = i;
    }
    bad[7] = std::nan("");
    EXPECT_THROWS(pnl_attribution::ksStatistic(good, bad));
    EXPECT_THROWS(pnl_attribution::test(bad, good));

    // Small-unit covariances are valid Monte Carlo inputs: the pivot
    // floor is relative to the diagonal scale, so two 0.5bp-vol rate
    // factors no longer read as "not positive-definite".
    double v = 2.5e-9;
    Matrix tiny = {{v, 0.9 * v}, {0.9 * v, v}};
    std::vector<double> expo = {1'000'000, -500'000};
    double dn = var_engine::deltaNormalVar(expo, tiny, 0.99);
    // The linear book agreement holds at any unit scale.
    EXPECT_NEAR(dn, var_engine::monteCarloVar(expo, tiny, 0.99, 100'000, 7).var,
                0.04 * dn);

    // PCA at extreme magnitudes: norm accumulators must not overflow.
    Pca huge(Matrix{{2e155, 1e155}, {1e155, 2e155}});
    EXPECT_NEAR(3e155, huge.eigenvalue(0), 1e144);
    EXPECT_NEAR(1e155, huge.eigenvalue(1), 1e144);

    // A confidence typo (99.9 instead of 0.999) throws, never NaN.
    evt::GpdFit fit{10, 0.3, 2, 100, 1000};
    EXPECT_THROWS(fit.var(99.9));
    EXPECT_TRUE(std::isfinite(fit.var(0.999)));

    // Aliased out/scratch would silently corrupt the dependence.
    GaussianCopula cop(Matrix{{1, 0.5}, {0.5, 1}});
    std::vector<double> u(2);
    JavaRandom rnd1(1);
    EXPECT_THROWS(cop.sample(rnd1, u, u));

    // A NaN exposure fails at the stress gate, not as NaN per scenario.
    EXPECT_THROWS(stress_tester::scenarioPnl(
            {1e6, std::nan(""), 0, 0, 0}, stress_tester::lehman2008()));

    // An indefinite "covariance" (typo'd correlation 1.3) fails loudly
    // instead of silently simulating a clamped dependence structure.
    EXPECT_THROWS(math_utils::cholesky(
            Matrix{{1e-4, 1.3e-4}, {1.3e-4, 1e-4}}));
}

QTEST_MAIN()
