// Port of VolatilityModelsTest (EWMA half), FormulaPinsTest's EWMA weight
// pins, the HAR-RV test from QuantSignalsTest, and InformationCriteriaTest.
// Java simulates with SplittableRandom/Random; a seeded mt19937 supplies the
// same SHAPE of data; all hand-derived pins are exact ports.
#include "quantfinlib/volatility/ewma_volatility.hpp"
#include "quantfinlib/volatility/har_rv.hpp"
#include "quantfinlib/volatility/information_criteria.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "quantfinlib/util/math_utils.hpp"
#include "qtest.hpp"

using quantfinlib::EwmaVolatility;
using quantfinlib::HarRv;
using quantfinlib::InformationCriteria;
namespace mu = quantfinlib::math_utils;

// ------------------------------------------------------------------ EWMA

TEST(ewmaReactsToVolatilityRegimeShift) {
    std::mt19937 rnd(5);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<double> r(600);
    for (int i = 0; i < 600; i++) {
        double vol = i < 500 ? 0.005 : 0.03;   // regime shift at t=500
        r[i] = vol * gauss(rnd);
    }
    EwmaVolatility ewma = EwmaVolatility::riskMetrics();
    double calm = ewma.latestVol(std::vector<double>(r.begin(), r.begin() + 500));
    double stressed = ewma.latestVol(r);
    EXPECT_TRUE(stressed > 2 * calm);
    // Variance series aligns with returns and stays positive.
    std::vector<double> h = ewma.variances(r);
    EXPECT_TRUE(h.size() == r.size());
    for (double v : h) {
        EXPECT_TRUE(v > 0);
    }
    EXPECT_THROWS(EwmaVolatility(1.5));
    EXPECT_THROWS(ewma.variances({0.01}));
}

// Port of FormulaPinsTest.ewmaRecursionWeightsPinned.
TEST(ewmaRecursionWeightsPinned) {
    // Repinned for the causal seed (finding H13), by hand. {0.01, -0.02}:
    // h[1] = r0^2 = 1e-4 and h[0] = h[1];
    // next = (0.94*0.06*1e-4 + 0.06*4e-4) / (0.94*0.06 + 0.06) = 2.964e-5 / 0.1164.
    // The prior form keeps the old recursion: seed 4.5e-4 gives
    // h[1] = 0.94*4.5e-4 + 0.06*0.01^2 = 4.29e-4 (a lambda swap gives 1.21e-4).
    EwmaVolatility ewma(0.94);
    std::vector<double> h = ewma.variances({0.01, -0.02});
    EXPECT_NEAR(h[0], 1e-4, 1e-15);
    EXPECT_NEAR(h[1], 1e-4, 1e-15);
    // lambda on the OLD variance, 1-lambda on r^2.
    EXPECT_NEAR(ewma.variances({0.01, -0.02}, 4.5e-4)[1], 4.29e-4, 1e-15);
    EXPECT_NEAR(ewma.latestVol({0.01, -0.02}), std::sqrt(2.964e-5 / 0.1164), 1e-12);
    // annualizedVol = latestVol * sqrt(ppy): 252 periods pins by definition.
    EXPECT_NEAR(ewma.annualizedVol({0.01, -0.02}, 252),
                std::sqrt(2.964e-5 / 0.1164) * std::sqrt(252.0), 1e-12);
}

// ---------------------------------------------------------------- HAR-RV

TEST(harRecoversPlantedCoefficientsAndForecastsSanely) {
    // Plant the HAR process itself: c=0.02, bd=0.4, bw=0.3, bm=0.2
    // (persistence 0.9, stationary mean 0.2), small uniform noise.
    std::mt19937 rnd(9);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    int n = 1500;
    std::vector<double> rv(n, 0.0);
    std::fill(rv.begin(), rv.begin() + 22, 0.2);
    for (int t = 21; t < n - 1; t++) {
        double w = mu::mean(rv, t - 4, t + 1);
        double m = mu::mean(rv, t - 21, t + 1);
        rv[t + 1] = std::max(1e-6, 0.02 + 0.4 * rv[t] + 0.3 * w + 0.2 * m
                                       + 0.005 * (2 * uni(rnd) - 1));
    }
    HarRv::Params p = HarRv::fit(rv);
    EXPECT_NEAR(p.betaDaily, 0.4, 0.08);     // daily horizon
    EXPECT_NEAR(p.betaWeekly, 0.3, 0.20);    // weekly horizon
    EXPECT_NEAR(p.betaMonthly, 0.2, 0.20);   // monthly horizon
    EXPECT_NEAR(p.intercept, 0.02, 0.03);

    double forecast = HarRv::forecast(rv, p);
    // The forecast sits near the stationary level.
    EXPECT_TRUE(forecast > 0.1 && forecast < 0.3);

    // The floor: a pathological parameter set cannot forecast a negative
    // variance.
    EXPECT_NEAR(HarRv::forecast(rv, HarRv::Params{-10, 0, 0, 0}), 0.0, 0.0);

    // Window alignment pinned EXACTLY where the horizons disagree:
    // 21 flat days then a spiked last day. d = 0.5, w = (4*0.1+0.5)/5
    // = 0.18, m = (21*0.1+0.5)/22; a d/w swap or off-by-one window moves
    // this a lot.
    std::vector<double> spiked(22, 0.1);
    spiked[21] = 0.5;
    HarRv::Params hand{0.01, 0.5, 0.3, 0.1};
    double expected = 0.01 + 0.5 * 0.5 + 0.3 * 0.18 + 0.1 * (21 * 0.1 + 0.5) / 22;
    EXPECT_NEAR(HarRv::forecast(spiked, hand), expected, 1e-12);

    EXPECT_THROWS(HarRv::fit(std::vector<double>(59)));
    std::vector<double> bad = rv;
    bad[100] = -0.1;
    EXPECT_THROWS(HarRv::fit(bad));
    EXPECT_THROWS(HarRv::forecast(std::vector<double>(21, 0.1), hand));
}

// -------------------------------------------------------------- AIC / BIC

TEST(informationCriteriaFormulasAreExact) {
    EXPECT_NEAR(InformationCriteria::aic(-100, 3), 2 * 3 - 2 * (-100.0), 0.0);
    EXPECT_NEAR(InformationCriteria::aic(-100, 3), 206.0, 0.0);
    EXPECT_NEAR(InformationCriteria::bic(-100, 3, 500),
                3 * std::log(500.0) - 2 * (-100.0), 0.0);
    // Zero parameters: the criteria are just -2 ln L.
    EXPECT_NEAR(InformationCriteria::aic(-100, 0), 200.0, 0.0);
    EXPECT_NEAR(InformationCriteria::bic(-100, 0, 500), 200.0, 0.0);
}

TEST(betterLikelihoodWinsAtEqualComplexity) {
    // Same k (and n): the model with the higher log-likelihood must score
    // LOWER on both criteria — lower is better.
    EXPECT_TRUE(InformationCriteria::aic(-95, 4) < InformationCriteria::aic(-100, 4));
    EXPECT_TRUE(InformationCriteria::bic(-95, 4, 250)
                < InformationCriteria::bic(-100, 4, 250));
}

TEST(bicPenalizesParametersHarderThanAicOnLargeSamples) {
    // One extra parameter costs 2 under AIC but ln(n) under BIC: for
    // n >= 8 (ln 8 > 2) BIC can reject an extra parameter AIC accepts.
    double gain = 1.2;   // log-likelihood improvement from the extra parameter
    double aicSimple = InformationCriteria::aic(-100, 1);
    double aicRich = InformationCriteria::aic(-100 + gain, 2);
    double bicSimple = InformationCriteria::bic(-100, 1, 1000);
    double bicRich = InformationCriteria::bic(-100 + gain, 2, 1000);
    EXPECT_TRUE(aicRich < aicSimple);   // AIC accepts: 2*1.2 > 2
    EXPECT_TRUE(bicRich > bicSimple);   // BIC rejects: 2*1.2 < ln(1000)
}

TEST(informationCriteriaGatesRefuseNonsense) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_THROWS(InformationCriteria::aic(nan, 3));
    EXPECT_THROWS(InformationCriteria::aic(inf, 3));
    EXPECT_THROWS(InformationCriteria::aic(-100, -1));
    EXPECT_THROWS(InformationCriteria::bic(-100, 3, 0));
    EXPECT_THROWS(InformationCriteria::bic(-inf, 3, 100));
}

QTEST_MAIN()
