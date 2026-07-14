// Port of VolatilityModelsTest (GARCH half), EgarchTest, the GJR tests from
// MarketRiskTest, and FormulaPinsTest.garchForecastHorizonExponentAnchored.
// Java simulates with SplittableRandom/Random; a seeded mt19937 supplies the
// same SHAPE of data (planted-parameter GARCH/GJR/EGARCH processes) and the
// assertions carry the same tolerances. Hand pins are exact ports.
#include "quantfinlib/volatility/egarch11.hpp"
#include "quantfinlib/volatility/garch11.hpp"
#include "quantfinlib/volatility/gjr_garch11.hpp"

#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

#include "quantfinlib/util/math_utils.hpp"
#include "qtest.hpp"

using quantfinlib::Egarch11;
using quantfinlib::Garch11;
using quantfinlib::GjrGarch11;
namespace mu = quantfinlib::math_utils;

namespace {

/** Simulates a GARCH(1,1) return series with known parameters. */
std::vector<double> simulateGarch(double omega, double alpha, double beta,
                                  int n, unsigned seed) {
    std::mt19937 rnd(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<double> r(n);
    double h = omega / (1 - alpha - beta);
    for (int t = 0; t < n; t++) {
        r[t] = std::sqrt(h) * gauss(rnd);
        h = omega + alpha * r[t] * r[t] + beta * h;
    }
    return r;
}

}  // namespace

// ---------------------------------------------------------------- Garch11

TEST(garchFitRecoversSimulatedParameters) {
    // True: alpha=0.08, beta=0.90, daily vol 1% (uncond var 1e-4).
    double uncond = 1e-4;
    std::vector<double> r = simulateGarch(uncond * 0.02, 0.08, 0.90, 4000, 42);
    Garch11::Params fit = Garch11::fit(r);

    EXPECT_TRUE(fit.persistence() > 0.90 && fit.persistence() < 0.9995);
    EXPECT_NEAR(fit.alpha, 0.08, 0.06);
    EXPECT_NEAR(fit.unconditionalVariance(), uncond, uncond * 0.35);
    EXPECT_TRUE(std::isfinite(fit.logLikelihood));
}

TEST(garchForecastMeanRevertsToUnconditional) {
    std::vector<double> r = simulateGarch(2e-6, 0.08, 0.90, 3000, 7);
    Garch11::Params fit = Garch11::fit(r);
    double shortHorizon = Garch11::forecastVariance(r, fit, 1);
    double longHorizon = Garch11::forecastVariance(r, fit, 5000);
    EXPECT_NEAR(longHorizon, fit.unconditionalVariance(),
                fit.unconditionalVariance() * 0.01);
    EXPECT_TRUE(shortHorizon > 0);
    // Conditional variances align with returns.
    std::vector<double> h = Garch11::conditionalVariances(r, fit);
    EXPECT_TRUE(h.size() == r.size());
    EXPECT_THROWS(Garch11::fit(std::vector<double>(50)));
    EXPECT_THROWS(Garch11::forecastVariance(r, fit, 0));
}

// Port of FormulaPinsTest.garchForecastHorizonExponentAnchored: the horizon
// exponent is persistence^{k-1}, anchored at k = 1 and 2.
TEST(garchForecastHorizonExponentAnchored) {
    Garch11::Params p{2e-6, 0.08, 0.9, 0};
    std::vector<double> r(300);
    std::mt19937 rnd(3);
    std::normal_distribution<double> gauss(0.0, 1.0);
    for (std::size_t i = 0; i < r.size(); i++) {
        r[i] = 0.01 * gauss(rnd);
    }
    double mean = mu::mean(r);
    std::vector<double> h = Garch11::conditionalVariances(r, p);
    double lastR = r.back() - mean;
    // Horizon 1 IS the one-step GARCH update (persistence^0 = 1).
    double oneStep = 2e-6 + 0.08 * lastR * lastR + 0.9 * h.back();
    EXPECT_NEAR(Garch11::forecastVariance(r, p, 1), oneStep, 1e-15);
    double uncond = p.unconditionalVariance();
    // Horizon 2: one persistence factor, not two.
    EXPECT_NEAR(Garch11::forecastVariance(r, p, 2),
                uncond + p.persistence() * (oneStep - uncond), 1e-15);
}

// ------------------------------------------------------------- GjrGarch11

TEST(gjrFindsTheLeverageEffectAndItsAbsenceHonestly) {
    // Simulate a GJR process with a strong planted leverage term.
    std::mt19937 rnd(42);
    std::normal_distribution<double> gauss(0.0, 1.0);
    int n = 4000;
    double omega = 2e-6;
    double alpha = 0.03;
    double gammaTrue = 0.12;
    double beta = 0.85;
    std::vector<double> r(n);
    double h = omega / (1 - alpha - gammaTrue / 2 - beta);
    for (int t = 0; t < n; t++) {
        r[t] = std::sqrt(h) * gauss(rnd);
        double arch = r[t] < 0 ? alpha + gammaTrue : alpha;
        h = omega + arch * r[t] * r[t] + beta * h;
    }
    GjrGarch11::Params fit = GjrGarch11::fit(r);
    EXPECT_TRUE(fit.gamma > 0.05);          // the planted asymmetry is found
    EXPECT_TRUE(fit.persistence() < 1);     // stationary fit
    // The model that generated the data likelihood-beats the symmetric one.
    EXPECT_TRUE(fit.logLikelihood > Garch11::fit(r).logLikelihood);

    // Symmetric GARCH data: the honest answer is gamma ~ 0.
    std::vector<double> sym(n);
    double hs = 2e-6 / (1 - 0.08 - 0.9);
    for (int t = 0; t < n; t++) {
        sym[t] = std::sqrt(hs) * gauss(rnd);
        hs = 2e-6 + 0.08 * sym[t] * sym[t] + 0.9 * hs;
    }
    EXPECT_TRUE(GjrGarch11::fit(sym).gamma < 0.06);

    // Forecast mean-reverts toward the unconditional variance.
    double uncond = fit.unconditionalVariance();
    double near = GjrGarch11::forecastVariance(r, fit, 1);
    double far = GjrGarch11::forecastVariance(r, fit, 500);
    EXPECT_TRUE(std::abs(far - uncond) < std::abs(near - uncond) + 1e-15);
    EXPECT_THROWS(GjrGarch11::fit(std::vector<double>(50)));
}

TEST(gjrGridIsNotAHiddenParameterCap) {
    // Low-persistence / high-ARCH data: true alpha = 0.45, beta = 0.15 — a
    // fit box capped at alpha <= 0.30 can only creep to ~0.37 across the
    // refinement passes and pins at the edge silently.
    std::mt19937 rnd(5);
    std::normal_distribution<double> gauss(0.0, 1.0);
    int n = 4000;
    std::vector<double> hot(n);
    double hv = 1e-4;
    for (int t = 0; t < n; t++) {
        hot[t] = std::sqrt(hv) * gauss(rnd);
        hv = 4e-5 + 0.45 * hot[t] * hot[t] + 0.15 * hv;
    }
    GjrGarch11::Params fit = GjrGarch11::fit(hot);
    EXPECT_TRUE(fit.alpha > 0.38);   // the MLE reaches the true high-ARCH region
    EXPECT_TRUE(fit.beta < 0.35);    // and the true low beta
}

// --------------------------------------------------------------- Egarch11

TEST(egarchFindsPlantedLeverageAndItsAbsence) {
    // Simulate EGARCH with strong planted leverage (gamma = -0.12).
    std::mt19937 rnd(42);
    std::normal_distribution<double> gauss(0.0, 1.0);
    int n = 4000;
    double beta = 0.9;
    double alpha = 0.15;
    double gamma = -0.12;
    double omega = (1 - beta) * std::log(1e-4);
    std::vector<double> r(n);
    double lnH = std::log(1e-4);
    for (int t = 0; t < n; t++) {
        double z = gauss(rnd);
        r[t] = std::exp(lnH / 2) * z;
        lnH = omega + beta * lnH
                + alpha * (std::abs(z) - std::sqrt(2 / std::numbers::pi)) + gamma * z;
    }
    Egarch11::Params fit = Egarch11::fit(r);
    EXPECT_TRUE(fit.gamma < -0.05);                   // leverage found AS A SIGN
    EXPECT_TRUE(fit.beta > 0.8 && fit.beta < 0.98);   // log-variance persistence
    EXPECT_TRUE(fit.alpha > 0.03);                    // the magnitude effect
    EXPECT_NEAR(fit.unconditionalLogVariance(), fit.omega / (1 - fit.beta), 1e-12);

    // Symmetric GARCH data: the honest answer is gamma ~ 0.
    std::vector<double> sym(n);
    double h = 2e-6 / (1 - 0.08 - 0.9);
    for (int t = 0; t < n; t++) {
        sym[t] = std::sqrt(h) * gauss(rnd);
        h = 2e-6 + 0.08 * sym[t] * sym[t] + 0.9 * h;
    }
    EXPECT_TRUE(std::abs(Egarch11::fit(sym).gamma) < 0.08);
}

TEST(leverageMeansDownMovesRaiseTomorrowsVolMore) {
    std::mt19937 rnd(7);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<double> base(200);
    for (std::size_t t = 0; t < base.size(); t++) {
        base[t] = 0.01 * gauss(rnd);
    }
    // Identical histories except the LAST return's sign.
    std::vector<double> down = base;
    std::vector<double> up = base;
    down.back() = -0.03;
    up.back() = 0.03;
    Egarch11::Params leveraged{0.1 * std::log(1e-4), 0.15, -0.12, 0.9, 0};
    double afterDown = Egarch11::nextVariance(down, leveraged);
    double afterUp = Egarch11::nextVariance(up, leveraged);
    // gamma < 0: the down move frightens tomorrow more.
    EXPECT_TRUE(afterDown > afterUp);

    // Every conditional variance is positive BY CONSTRUCTION — the log
    // form needs no parameter constraints to guarantee it.
    std::vector<double> variances = Egarch11::conditionalVariances(down, leveraged);
    for (double v : variances) {
        EXPECT_TRUE(v > 0);
    }
    EXPECT_THROWS(Egarch11::fit(std::vector<double>(50)));
    // Zero variance is not a fittable series.
    EXPECT_THROWS(Egarch11::fit(std::vector<double>(200)));
}

TEST(egarchOneStepRecursionMatchesHandArithmeticExactly) {
    // Two returns {0.01, -0.02}: mean -0.005, sample var 4.5e-4.
    // z1 = 0.015/sqrt(4.5e-4) = 1/sqrt(2); the recursion is then fully
    // determined — computed by hand below and pinned.
    Egarch11::Params p{0.1 * std::log(1e-4), 0.15, -0.12, 0.9, 0};
    std::vector<double> r = {0.01, -0.02};
    double lnH0 = std::log(4.5e-4);
    double z1 = 0.015 / std::sqrt(4.5e-4);
    double lnH1 = p.omega + 0.9 * lnH0
            + 0.15 * (z1 - std::sqrt(2 / std::numbers::pi)) - 0.12 * z1;
    std::vector<double> cv = Egarch11::conditionalVariances(r, p);
    EXPECT_NEAR(cv[0], 4.5e-4, 1e-18);              // seeded at the sample variance
    EXPECT_NEAR(cv[1], std::exp(lnH1), 1e-15);      // the exact one-step transition

    double z2 = -0.015 / std::sqrt(cv[1]);
    double lnH2 = p.omega + 0.9 * std::log(cv[1])
            + 0.15 * (std::abs(z2) - std::sqrt(2 / std::numbers::pi)) - 0.12 * z2;
    // nextVariance IS the recursion's next step, nothing else.
    EXPECT_NEAR(Egarch11::nextVariance(r, p), std::exp(lnH2), 1e-15);

    // The house gates: a constant series must throw, never return h = 0
    // (which would contradict "positive by construction").
    EXPECT_THROWS(Egarch11::conditionalVariances({0.01, 0.01, 0.01}, p));
    EXPECT_THROWS(Egarch11::nextVariance(
            {0.01, std::numeric_limits<double>::quiet_NaN()}, p));
    EXPECT_THROWS(Egarch11::conditionalVariances({0.01}, p));
}

QTEST_MAIN()
