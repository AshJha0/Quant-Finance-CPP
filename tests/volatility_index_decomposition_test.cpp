// Port of VolatilityIndexAndDecompositionTest (Java): the fear gauge and the
// systematic/idiosyncratic split. The Java test prices the option chain with
// pricing.BlackScholes; that domain is not part of this port, so a minimal
// Black-Scholes price (r = q = 0, built on math_utils::normCdf exactly as the
// Java pricer is) lives locally in this test. The beta cross-check against
// risk.RiskMetrics.beta is replaced by the same identity computed directly
// from math_utils covariance/variance.
#include "quantfinlib/volatility/volatility_decomposition.hpp"
#include "quantfinlib/volatility/volatility_index.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "quantfinlib/util/math_utils.hpp"
#include "qtest.hpp"

using quantfinlib::VolatilityDecomposition;
using quantfinlib::VolatilityIndex;
namespace mu = quantfinlib::math_utils;

namespace {

// Undiscounted (r = 0, q = 0) Black-Scholes price for chain construction.
double bsPrice(bool isCall, double spot, double strike, double vol, double t) {
    double sqrtT = std::sqrt(t);
    double d1 = (std::log(spot / strike) + 0.5 * vol * vol * t) / (vol * sqrtT);
    double d2 = d1 - vol * sqrtT;
    if (isCall) {
        return spot * mu::normCdf(d1) - strike * mu::normCdf(d2);
    }
    return strike * mu::normCdf(-d2) - spot * mu::normCdf(-d1);
}

}  // namespace

// ------------------------------------------------------------------
// VIX-style index — model-free means it must RECOVER a flat vol
// ------------------------------------------------------------------

TEST(indexRecoversAFlatVolAndReadsTheSmile) {
    // A chain priced at flat 20% vol: the variance-swap replication must
    // hand the 20% back — that is what "model-free" MEANS.
    double t = 30.0 / 365;
    int n = 81;
    std::vector<double> strikes(n), puts(n), calls(n);
    for (int i = 0; i < n; i++) {
        strikes[i] = 60 + i;                  // 60..140: 8.9 sigma down, 5.9 up
        // max(0, .): the normCdf approximation can leave deep-OTM prices
        // at ~-1e-9; real chains quote >= 0 by construction.
        puts[i] = std::max(0.0, bsPrice(false, 100, strikes[i], 0.20, t));
        calls[i] = std::max(0.0, bsPrice(true, 100, strikes[i], 0.20, t));
    }
    double flat = VolatilityIndex::index(strikes, puts, calls, 100, 0, t);
    EXPECT_NEAR(flat, 0.20, 2e-3);   // the fear gauge reads the market's number

    // F STRICTLY BETWEEN strikes: with F on a strike (above) the
    // (F/K0-1)^2 correction term is exactly zero and untestable — dropping
    // it would pass. At F = 100.9 the omission error is ~+2.9e-3, so THIS
    // assertion is what makes the term load-bearing (chain re-priced at
    // the shifted forward via spot = 100.9, r = 0).
    std::vector<double> putsF(n), callsF(n);
    for (int i = 0; i < n; i++) {
        putsF[i] = std::max(0.0, bsPrice(false, 100.9, strikes[i], 0.20, t));
        callsF[i] = std::max(0.0, bsPrice(true, 100.9, strikes[i], 0.20, t));
    }
    double offGrid = VolatilityIndex::index(strikes, putsF, callsF, 100.9, 0, t);
    EXPECT_NEAR(offGrid, 0.20, 2e-3);   // the K0 != F correction earns its keep

    // A put SKEW (downside priced at 25%) must RAISE the index above the
    // ATM 20% — the wings carry real premium and the replication weights
    // them in. That is why VIX > ATM implied vol.
    std::vector<double> skewedPuts(n);
    for (int i = 0; i < n; i++) {
        skewedPuts[i] = std::max(0.0, bsPrice(false, 100, strikes[i], 0.25, t));
    }
    double skewed = VolatilityIndex::index(strikes, skewedPuts, calls, 100, 0, t);
    EXPECT_TRUE(skewed > flat + 0.005 && skewed < 0.25);

    // Gates: extrapolation is an opinion, not a measurement.
    EXPECT_THROWS(VolatilityIndex::index(strikes, puts, calls, 150, 0, t));
    EXPECT_THROWS(VolatilityIndex::index({90, 100}, std::vector<double>(2),
                                         std::vector<double>(2), 95, 0, t));
    // An all-zero chain implies no variance: inconsistent.
    EXPECT_THROWS(VolatilityIndex::index(strikes, std::vector<double>(n),
                                         std::vector<double>(n), 100, 0, t));
}

// ------------------------------------------------------------------
// Systematic vs idiosyncratic — the split is EXACT, not approximate
// ------------------------------------------------------------------

TEST(decompositionRecoversPlantedBetaAndSplitsExactly) {
    std::mt19937 rnd(11);
    std::normal_distribution<double> gauss(0.0, 1.0);
    int n = 5000;
    std::vector<double> market(n), asset(n);
    for (int i = 0; i < n; i++) {
        market[i] = 0.02 * gauss(rnd);
        asset[i] = 1.5 * market[i] + 0.01 * gauss(rnd);
    }
    auto d = VolatilityDecomposition::decompose(asset, market);
    EXPECT_NEAR(d.beta, 1.5, 0.05);   // the planted beta
    // One beta in this library: Cov(a,m)/Var(m), cross-checked directly.
    EXPECT_NEAR(d.beta, mu::covariance(asset, market) / mu::variance(market), 1e-12);
    // The residual is the planted 1% noise.
    EXPECT_NEAR(d.idiosyncraticVariance, 1e-4, 1.5e-5);
    // The OLS split is an identity, not an approximation.
    EXPECT_NEAR(d.systematicVariance + d.idiosyncraticVariance, d.totalVariance, 1e-18);
    EXPECT_TRUE(d.rSquared > 0.85);   // mostly a market story
    EXPECT_NEAR(d.systematicVol(252), std::sqrt(d.systematicVariance * 252), 1e-15);
    EXPECT_NEAR(d.idiosyncraticVol(252), std::sqrt(d.idiosyncraticVariance * 252), 1e-15);
    EXPECT_NEAR(d.totalVol(252), std::sqrt(d.totalVariance * 252), 1e-15);

    // A clone of the market IS the market: beta 1, idio 0, R^2 1.
    auto clone = VolatilityDecomposition::decompose(market, market);
    EXPECT_NEAR(clone.beta, 1.0, 1e-12);
    EXPECT_NEAR(clone.idiosyncraticVariance, 0.0, 1e-15);   // no company story
    EXPECT_NEAR(clone.rSquared, 1.0, 1e-12);

    // Pure noise vs the market: nothing systematic to find.
    std::vector<double> noise(n);
    for (int i = 0; i < n; i++) {
        noise[i] = 0.015 * gauss(rnd);
    }
    auto idio = VolatilityDecomposition::decompose(noise, market);
    EXPECT_TRUE(idio.rSquared < 0.02);   // a biotech, not a utility

    // Misaligned series.
    EXPECT_THROWS(VolatilityDecomposition::decompose(std::vector<double>(30), market));
    // A flat benchmark decomposes nothing.
    EXPECT_THROWS(VolatilityDecomposition::decompose(noise, std::vector<double>(n)));
    std::vector<double> bad = asset;
    bad[7] = std::numeric_limits<double>::quiet_NaN();
    EXPECT_THROWS(VolatilityDecomposition::decompose(bad, market));
}

QTEST_MAIN()
