// Port of the VarEngine sections of com.quantfinlib.risk.MarketRiskTest
// (Java): the four VaR flavors agree where they should and diverge
// where they must, plus full-revaluation VaR through a std::function
// pricer. Monte Carlo replays the EXACT java.util.Random stream (seed
// 42) via JavaRandom, so the Java tolerances carry unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/var_engine.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace var_engine = quantfinlib::var_engine;
namespace math_utils = quantfinlib::math_utils;
using Matrix = var_engine::Matrix;

TEST(theFourVarFlavorsAgreeWhereTheyShouldAndDivergeWhereTheyMust) {
    std::vector<double> exposures = {1'000'000, -500'000};
    Matrix cov = {{4e-4, 1e-4}, {1e-4, 2.25e-4}};  // 2%/1.5% daily, corr ~1/3

    double sigma = var_engine::portfolioStdev(exposures, cov);
    double parametric = var_engine::deltaNormalVar(exposures, cov, 0.99);
    // Delta-normal IS z times sigma.
    EXPECT_NEAR(math_utils::normInv(0.99) * sigma, parametric, 1e-9);
    // ES sits beyond VaR, always.
    EXPECT_TRUE(var_engine::deltaNormalEs(exposures, cov, 0.99) > parametric);

    // Monte Carlo on the same linear book converges to delta-normal.
    auto mc = var_engine::monteCarloVar(exposures, cov, 0.99, 200'000, 42);
    EXPECT_NEAR(parametric, mc.var, 0.03 * parametric);
    EXPECT_TRUE(mc.expectedShortfall > mc.var);

    // Short gamma makes the left tail worse than delta-normal admits.
    Matrix shortGamma = {{-4'000'000, 0}, {0, 0}};
    double dgVar = var_engine::deltaGammaVar(exposures, shortGamma, cov, 0.99);
    EXPECT_TRUE(dgVar > parametric);
    Matrix longGamma = {{4'000'000, 0}, {0, 0}};
    // Long gamma cushions the same tail.
    EXPECT_TRUE(var_engine::deltaGammaVar(exposures, longGamma, cov, 0.99) < parametric);

    // Delta-gamma ES: gamma = 0 reduces exactly to delta-normal ES.
    Matrix zeroGamma(2, std::vector<double>(2, 0.0));
    EXPECT_NEAR(var_engine::deltaNormalEs(exposures, cov, 0.99),
                var_engine::deltaGammaEs(exposures, zeroGamma, cov, 0.99), 1e-9);
    double dgEs = var_engine::deltaGammaEs(exposures, shortGamma, cov, 0.99);
    // ES sits beyond VaR for the gamma book too.
    EXPECT_TRUE(dgEs > dgVar);
    // Short gamma worsens the tail MEAN, not just the quantile.
    EXPECT_TRUE(dgEs > var_engine::deltaNormalEs(exposures, cov, 0.99));

    // Historical on a hand-built sample: the 95% loss of 100 scenarios
    // is the 95th-worst — row 94: loss = 1e6 * 95e-5 = 950.
    Matrix history(100, std::vector<double>(2, 0.0));
    for (int s = 0; s < 100; s++) {
        history[s][0] = -(s + 1) * 1e-5;  // factor 1 loses linearly more
    }
    auto hist = var_engine::historicalVar({1'000'000, 0}, history, 0.95);
    EXPECT_NEAR(950'000 * 1e-3, hist.var, 1e-6);
    EXPECT_TRUE(hist.expectedShortfall > hist.var);
}

TEST(fullRevaluationSeesWhatSensitivitiesMiss) {
    // 100 one-factor scenarios, monotonically worse: x_s = -(s+1)*5e-4.
    Matrix scenarios(100, std::vector<double>(1));
    for (int s = 0; s < 100; s++) {
        scenarios[s][0] = -(s + 1) * 5e-4;
    }
    // A LINEAR pricer reproduces historical simulation exactly.
    double delta = 1'000'000;
    auto linear = var_engine::fullRevaluationVar(
            scenarios, [delta](const std::vector<double>& moves) {
                return delta * moves[0];
            }, 0.99);
    auto historical = var_engine::historicalVar({delta}, scenarios, 0.99);
    EXPECT_NEAR(historical.var, linear.var, 1e-9);
    EXPECT_NEAR(historical.expectedShortfall, linear.expectedShortfall, 1e-9);

    // A SHORT-GAMMA pricer: full revaluation sees the curvature the
    // linear replay cannot. 99% row is s = 98 (x = -0.0495):
    // linear 49,500 + quadratic 0.5*4e7*0.0495^2 = 49,005 -> 98,505.
    double gamma = -4e7;
    auto quadratic = var_engine::fullRevaluationVar(
            scenarios, [delta, gamma](const std::vector<double>& moves) {
                return delta * moves[0] + 0.5 * gamma * moves[0] * moves[0];
            }, 0.99);
    EXPECT_NEAR(98'505, quadratic.var, 1e-6);
    EXPECT_TRUE(quadratic.var > linear.var);
    EXPECT_TRUE(quadratic.expectedShortfall > quadratic.var);

    // A pricer that cannot price a scenario is a modelling problem.
    EXPECT_THROWS(var_engine::fullRevaluationVar(
            scenarios, [](const std::vector<double>&) {
                return std::nan("");
            }, 0.99));
    // Fewer than 20 scenarios is not a tail.
    Matrix ten(10, std::vector<double>(1, 0.0));
    EXPECT_THROWS(var_engine::fullRevaluationVar(
            ten, [](const std::vector<double>&) { return 0.0; }, 0.99));
}

QTEST_MAIN()
