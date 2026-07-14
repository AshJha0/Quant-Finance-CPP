// Port of com.quantfinlib.risk.RiskMetricsTest (Java), minus the
// PortfolioRiskAnalyzer / RiskMetricRegistry cases (those classes are
// Phase 2). The Java test drew Gaussians from SplittableRandom; the
// C++ port draws from JavaRandom (java.util.Random LCG) — every
// assertion here is distributional or exact-arithmetic, none pins a
// specific SplittableRandom stream value.
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/java_random.hpp"
#include "quantfinlib/risk/risk_metrics.hpp"

using quantfinlib::JavaRandom;
namespace risk_metrics = quantfinlib::risk_metrics;

namespace {

std::vector<double> gaussianReturns(int n, double mean, double std, long long seed) {
    JavaRandom rnd(seed);
    std::vector<double> r(n);
    for (int i = 0; i < n; i++) {
        r[i] = mean + std * rnd.nextGaussian();
    }
    return r;
}

}  // namespace

TEST(maxDrawdownKnownValue) {
    // Peak 120 -> trough 90: (120 - 90) / 120 = 0.25 exactly.
    std::vector<double> equity = {100, 120, 90, 130, 130};
    EXPECT_NEAR(0.25, risk_metrics::maxDrawdown(equity), 1e-12);
}

TEST(historicalVarMatchesQuantile) {
    // 100 returns: -0.50, -0.49, ..., +0.49 — 5th percentile sits near -0.4505.
    std::vector<double> r(100);
    for (int i = 0; i < 100; i++) {
        r[i] = (i - 50) / 100.0;
    }
    double var95 = risk_metrics::historicalVar(r, 0.95);
    EXPECT_TRUE(var95 > 0.44 && var95 < 0.46);
}

TEST(cvarAtLeastVar) {
    std::vector<double> r = gaussianReturns(5000, 0.0, 0.02, 42);
    double var = risk_metrics::historicalVar(r, 0.95);
    double cvar = risk_metrics::conditionalVar(r, 0.95);
    EXPECT_TRUE(cvar >= var);
}

TEST(parametricVarCloseToHistoricalForGaussian) {
    // 99% quantile standard error at n = 20000, sigma = 1%:
    // sqrt(p(1-p)/n)/f(q) ~ 2.6e-4, so tol 0.002 is ~7.6 SE.
    std::vector<double> r = gaussianReturns(20000, 0.0002, 0.01, 7);
    double hist = risk_metrics::historicalVar(r, 0.99);
    double param = risk_metrics::parametricVar(r, 0.99);
    EXPECT_NEAR(hist, param, 0.002);
}

TEST(sharpePositiveForPositiveDrift) {
    // Drift 0.001 vs mean SE 0.01/sqrt(2000) ~ 2.2e-4: sign is robust.
    std::vector<double> r = gaussianReturns(2000, 0.001, 0.01, 3);
    EXPECT_TRUE(risk_metrics::sharpeRatio(r, 0, 252) > 0);
    EXPECT_TRUE(risk_metrics::sortinoRatio(r, 0, 252) > 0);
}

TEST(betaOfSelfIsOne) {
    std::vector<double> r = gaussianReturns(1000, 0.0, 0.015, 5);
    EXPECT_NEAR(1.0, risk_metrics::beta(r, r), 1e-9);
    EXPECT_NEAR(1.0, risk_metrics::correlation(r, r), 1e-9);
}

QTEST_MAIN()
