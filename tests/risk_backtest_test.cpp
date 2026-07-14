// Port of com.quantfinlib.risk.VarBacktestTest (Java). The Java test
// drew Gaussians from SplittableRandom; the C++ port draws from
// JavaRandom (java.util.Random LCG) — every assertion below is a
// distributional property of a correct N(0, vol) sample (exception
// counts, two-sided Kupiec, clustering), not a pin on a specific
// SplittableRandom stream value. The clustered/scattered case is fully
// deterministic and carries exactly.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/risk/java_random.hpp"
#include "quantfinlib/risk/var_backtest.hpp"

using quantfinlib::JavaRandom;
namespace var_backtest = quantfinlib::var_backtest;

namespace {

std::vector<double> gaussianReturns(int n, double vol, long long seed) {
    JavaRandom rnd(seed);
    std::vector<double> r(n);
    for (int i = 0; i < n; i++) {
        r[i] = vol * rnd.nextGaussian();
    }
    return r;
}

}  // namespace

TEST(wellCalibratedVarPassesAllTests) {
    std::vector<double> returns = gaussianReturns(2'000, 0.01, 42);
    // True 95% VaR of N(0, 1%) is 1.645%.
    auto r = var_backtest::test(returns, 0.01645, 0.95);

    EXPECT_TRUE(r.observations == 2'000);
    EXPECT_NEAR(100, r.expectedExceptions, 1e-9);
    EXPECT_TRUE(std::abs(r.exceptions - 100) < 30);
    EXPECT_TRUE(r.calibrated(0.05));
    EXPECT_TRUE(r.independent(0.05));
    EXPECT_TRUE(r.passes(0.05));
    // Exact chi-square(2) survival for the joint statistic.
    EXPECT_NEAR(std::exp(-r.conditionalCoverageStatistic / 2),
                r.conditionalCoveragePValue, 1e-12);
}

TEST(underestimatedVarIsRejected) {
    std::vector<double> returns = gaussianReturns(2'000, 0.01, 7);
    // Claiming 95% coverage with roughly a 68% quantile: far too many exceptions.
    auto r = var_backtest::test(returns, 0.010, 0.95);
    EXPECT_TRUE(r.exceptions > 200);
    EXPECT_TRUE(!r.calibrated(0.01));
    EXPECT_TRUE(!r.passes(0.01));
}

TEST(overestimatedVarIsAlsoRejected) {
    // Kupiec is two-sided: a VaR so conservative it never breaks also fails.
    std::vector<double> returns = gaussianReturns(2'000, 0.01, 9);
    auto r = var_backtest::test(returns, 0.05, 0.95);
    EXPECT_TRUE(r.exceptions == 0);
    EXPECT_TRUE(!r.calibrated(0.01));
}

TEST(clusteredExceptionsFailIndependence) {
    // Two series with identical exception COUNTS (25/500) but different timing.
    int n = 500;
    std::vector<double> clustered(n, 0.0);
    std::vector<double> scattered(n, 0.0);
    for (int i = 0; i < 25; i++) {
        clustered[100 + i] = -0.05;  // one 25-day crisis
        scattered[i * 20] = -0.05;   // evenly spread
    }
    auto bad = var_backtest::test(clustered, 0.02, 0.95);
    auto good = var_backtest::test(scattered, 0.02, 0.95);

    EXPECT_TRUE(bad.exceptions == good.exceptions);
    EXPECT_NEAR(bad.kupiecStatistic, good.kupiecStatistic, 1e-9);  // same rate
    EXPECT_TRUE(!bad.independent(0.01));
    EXPECT_TRUE(good.independent(0.05));
    EXPECT_TRUE(!bad.passes(0.01));  // conditional coverage catches the clustering
}

TEST(perPeriodForecastsAreSupported) {
    // GARCH-style varying forecasts: scale VaR with a known vol path.
    JavaRandom rnd(11);
    int n = 1'000;
    std::vector<double> returns(n);
    std::vector<double> var(n);
    for (int i = 0; i < n; i++) {
        double vol = i < 500 ? 0.005 : 0.02;  // regime shift
        returns[i] = vol * rnd.nextGaussian();
        var[i] = 1.645 * vol;                 // correctly scaled per period
    }
    auto adaptive = var_backtest::test(returns, var, 0.95);
    EXPECT_TRUE(adaptive.passes(0.05));

    // A constant VaR calibrated to the calm regime fails on the same data.
    auto constant = var_backtest::test(returns, 1.645 * 0.005, 0.95);
    EXPECT_TRUE(!constant.passes(0.01));
}

QTEST_MAIN()
