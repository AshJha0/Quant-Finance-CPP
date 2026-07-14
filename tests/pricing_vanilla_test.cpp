// Ports of the Java pricing tests for the vanilla layer: BlackScholesTest,
// the Black-76 and higher-order-Greeks sections of MarketRiskPricingTest,
// AmericanAndSabrTest (CRR tree + SABR), the SABR pins from
// FormulaPinsTest, and IncrementalGreeksTest (minus the JVM-only
// allocation probe). Every pinned value transfers at the Java tolerance.
#include <cmath>

#include "qtest.hpp"
#include "quantfinlib/pricing/binomial_tree.hpp"
#include "quantfinlib/pricing/black76.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/pricing/higher_order_greeks.hpp"
#include "quantfinlib/pricing/incremental_greeks.hpp"
#include "quantfinlib/pricing/sabr_model.hpp"

using quantfinlib::IncrementalGreeks;
using quantfinlib::OptionType;
namespace bs = quantfinlib::black_scholes;
namespace b76 = quantfinlib::black76;
namespace hog = quantfinlib::higher_order_greeks;
namespace tree = quantfinlib::binomial_tree;
namespace sabr = quantfinlib::sabr_model;

// Textbook case: S=100, K=100, r=5%, q=0, sigma=20%, T=1.
namespace {
constexpr double S = 100, K = 100, R = 0.05, Q = 0, VOL = 0.2, T = 1;
}

TEST(bs_matchesTextbookValues) {
    // Hull's canonical example: c = 10.4506, p = 5.5735 by put-call parity,
    // delta = N(d1) = 0.6368, gamma 0.0188, vega 37.524, theta -6.414/yr,
    // rho 53.232 — the exact pins from the Java test.
    EXPECT_NEAR(bs::price(OptionType::CALL, S, K, R, Q, VOL, T), 10.4506, 1e-3);
    EXPECT_NEAR(bs::price(OptionType::PUT, S, K, R, Q, VOL, T), 5.5735, 1e-3);
    EXPECT_NEAR(bs::delta(OptionType::CALL, S, K, R, Q, VOL, T), 0.6368, 1e-3);
    EXPECT_NEAR(bs::gamma(S, K, R, Q, VOL, T), 0.0188, 1e-3);
    EXPECT_NEAR(bs::vega(S, K, R, Q, VOL, T), 37.524, 1e-2);
    EXPECT_NEAR(bs::theta(OptionType::CALL, S, K, R, Q, VOL, T), -6.414, 1e-2);
    EXPECT_NEAR(bs::rho(OptionType::CALL, S, K, R, Q, VOL, T), 53.232, 1e-2);
}

TEST(bs_putCallParityHoldsWithCarry) {
    // C - P = S*e^(-qT) - K*e^(-rT), q = foreign rate (Garman-Kohlhagen).
    double q = 0.03;
    double call = bs::price(OptionType::CALL, S, K, R, q, VOL, T);
    double put = bs::price(OptionType::PUT, S, K, R, q, VOL, T);
    EXPECT_NEAR(call - put, S * std::exp(-q * T) - K * std::exp(-R * T), 1e-9);
}

TEST(bs_greeksRecordConsistentWithIndividualFunctions) {
    bs::Greeks g = bs::greeks(OptionType::PUT, S, K, R, Q, VOL, T);
    EXPECT_NEAR(g.price, bs::price(OptionType::PUT, S, K, R, Q, VOL, T), 1e-12);
    EXPECT_NEAR(g.delta, bs::delta(OptionType::PUT, S, K, R, Q, VOL, T), 1e-12);
    EXPECT_TRUE(g.delta < 0 && g.gamma > 0 && g.vega > 0);
}

TEST(bs_impliedVolRoundTrips) {
    double price = bs::price(OptionType::CALL, S, 110, R, Q, 0.27, 0.5);
    double iv = bs::impliedVol(OptionType::CALL, price, S, 110, R, Q, 0.5);
    EXPECT_NEAR(iv, 0.27, 1e-6);
}

TEST(bs_impliedVolUnattainablePriceIsNaN) {
    // Below intrinsic: no vol can produce it — NaN, not a bracket endpoint.
    EXPECT_TRUE(std::isnan(bs::impliedVol(OptionType::CALL, 0.0, S, 50, R, Q, 0.5)));
}

TEST(bs_expiryCollapsesToIntrinsic) {
    EXPECT_NEAR(bs::price(OptionType::CALL, 105, 100, R, Q, VOL, 0), 5, 1e-12);
    EXPECT_NEAR(bs::price(OptionType::PUT, 105, 100, R, Q, VOL, 0), 0, 1e-12);
    EXPECT_NEAR(bs::delta(OptionType::CALL, 105, 100, R, Q, VOL, 0), 1, 1e-12);
    EXPECT_NEAR(bs::gamma(105, 100, R, Q, VOL, 0), 0, 1e-12);
}

// ---- Black-76 (MarketRiskPricingTest section) --------------------------

TEST(black76_isBlackScholesWithZeroCarryOnTheForward) {
    double f = 102, k = 100, r = 0.03, vol = 0.25, t = 0.75;
    // The identity in the house convention: BlackScholes' carry is the
    // YIELD q, so a driftless forward means q = r.
    EXPECT_NEAR(b76::price(OptionType::CALL, f, k, r, vol, t),
                bs::price(OptionType::CALL, f, k, r, r, vol, t), 1e-10);
    // Put-call parity on the forward: C - P = df*(F - K).
    double c = b76::price(OptionType::CALL, f, k, r, vol, t);
    double p = b76::price(OptionType::PUT, f, k, r, vol, t);
    EXPECT_NEAR(c - p, std::exp(-r * t) * (f - k), 1e-10);
    // Implied vol round-trips.
    EXPECT_NEAR(b76::impliedVol(OptionType::CALL, c, f, k, r, t), vol, 1e-6);
    // Delta bounds and vega positivity.
    EXPECT_TRUE(b76::delta(OptionType::CALL, f, k, r, vol, t) > 0.5);
    EXPECT_TRUE(b76::vega(f, k, r, vol, t) > 0);
    // Zero vol = discounted intrinsic: df * (102 - 100).
    EXPECT_NEAR(b76::price(OptionType::CALL, f, k, r, 0, t),
                std::exp(-r * t) * 2, 1e-12);
}

// ---- Higher-order Greeks vs finite differences -------------------------

TEST(hog_vannaAndVolgaMatchFiniteDifferencesOfDeltaAndVega) {
    // Deliberately r != 2*carry etc. — parameters chosen so no convention
    // coincidence can fake agreement.
    double s = 100, k = 110, r = 0.02, carry = 0.035, vol = 0.3, t = 0.5;
    double h = 1e-4;

    double vannaFd = (bs::delta(OptionType::CALL, s, k, r, carry, vol + h, t)
            - bs::delta(OptionType::CALL, s, k, r, carry, vol - h, t)) / (2 * h);
    EXPECT_NEAR(hog::vanna(s, k, r, carry, vol, t), vannaFd, 1e-4);

    double volgaFd = (bs::vega(s, k, r, carry, vol + h, t)
            - bs::vega(s, k, r, carry, vol - h, t)) / (2 * h);
    EXPECT_NEAR(hog::volga(s, k, r, carry, vol, t), volgaFd, 1e-3);

    // An OTM call's volga is positive (long wings love vol of vol).
    EXPECT_TRUE(hog::volga(s, k, r, carry, vol, t) > 0);
    // Exchange-option cross-gamma is negative and shrinks as correlation rises.
    double lowCorr = hog::exchangeCrossGamma(100, 100, 0.3, 0.3, 0.2, 1);
    double highCorr = hog::exchangeCrossGamma(100, 100, 0.3, 0.3, 0.8, 1);
    EXPECT_TRUE(lowCorr < 0 && highCorr < 0);
    EXPECT_TRUE(std::fabs(highCorr) > std::fabs(lowCorr));
}

// ---- CRR binomial tree (AmericanAndSabrTest) ---------------------------

TEST(tree_europeanConvergesToBlackScholes) {
    double bsc = bs::price(OptionType::CALL, 100, 100, 0.05, 0, 0.2, 1);
    double tr = tree::price(OptionType::CALL, tree::ExerciseStyle::EUROPEAN,
                            100, 100, 0.05, 0, 0.2, 1, 500);
    EXPECT_NEAR(tr, bsc, 0.05);

    double bsPut = bs::price(OptionType::PUT, 100, 95, 0.03, 0.01, 0.25, 0.5);
    double treePut = tree::price(OptionType::PUT, tree::ExerciseStyle::EUROPEAN,
                                 100, 95, 0.03, 0.01, 0.25, 0.5, 500);
    EXPECT_NEAR(treePut, bsPut, 0.05);
}

TEST(tree_americanPutCarriesEarlyExercisePremium) {
    // Deep ITM American put on a high-rate asset: early exercise is valuable.
    double premium = tree::earlyExercisePremium(
            OptionType::PUT, 80, 100, 0.08, 0, 0.2, 1, 400);
    EXPECT_TRUE(premium > 0.1);
    // American never below intrinsic (20).
    double american = tree::price(OptionType::PUT, tree::ExerciseStyle::AMERICAN,
                                  80, 100, 0.08, 0, 0.2, 1, 400);
    EXPECT_TRUE(american >= 20 - 1e-9);
}

TEST(tree_americanCallWithoutCarryEqualsEuropean) {
    // No dividends: never optimal to exercise a call early.
    double premium = tree::earlyExercisePremium(
            OptionType::CALL, 100, 95, 0.05, 0, 0.25, 1, 400);
    EXPECT_NEAR(premium, 0, 1e-9);
}

TEST(tree_deltaMatchesBlackScholesForEuropean) {
    double bsDelta = bs::delta(OptionType::CALL, 100, 100, 0.05, 0, 0.2, 1);
    double treeDelta = tree::delta(OptionType::CALL, tree::ExerciseStyle::EUROPEAN,
                                   100, 100, 0.05, 0, 0.2, 1, 500);
    EXPECT_NEAR(treeDelta, bsDelta, 0.01);
}

TEST(tree_rejectsZeroSteps) {
    EXPECT_THROWS(tree::price(OptionType::CALL, tree::ExerciseStyle::EUROPEAN,
                              100, 100, 0.05, 0, 0.2, 1, 0));
}

// ---- SABR ---------------------------------------------------------------

namespace {
constexpr double F = 100, TS = 1, BETA = 1.0;
constexpr double ALPHA = 0.20, RHO = -0.30, NU = 0.60;
}

TEST(sabr_haganAtmFormulaConsistentWithSmile) {
    double atm = sabr::impliedVol(F, F, TS, ALPHA, BETA, RHO, NU);
    double nearAtm = sabr::impliedVol(F, F + 1e-6, TS, ALPHA, BETA, RHO, NU);
    EXPECT_NEAR(nearAtm, atm, 1e-6);
    // Negative rho: downside strikes carry higher vol.
    EXPECT_TRUE(sabr::impliedVol(F, 80, TS, ALPHA, BETA, RHO, NU)
            > sabr::impliedVol(F, 120, TS, ALPHA, BETA, RHO, NU));
}

TEST(sabr_calibrationRecoversGeneratedSmile) {
    std::vector<double> strikes{70, 80, 90, 100, 110, 120, 130};
    std::vector<double> vols(strikes.size());
    for (std::size_t i = 0; i < strikes.size(); i++) {
        vols[i] = sabr::impliedVol(F, strikes[i], TS, ALPHA, BETA, RHO, NU);
    }
    sabr::Params fit = sabr::calibrate(F, TS, BETA, strikes, vols);

    EXPECT_TRUE(fit.rmse < 5e-4);
    EXPECT_NEAR(fit.alpha, ALPHA, 0.02);
    EXPECT_NEAR(fit.rho, RHO, 0.10);
    EXPECT_NEAR(fit.nu, NU, 0.15);
    // Fitted smile reproduces an unquoted strike.
    double interp = sabr::impliedVol(F, 85, TS, fit.alpha, fit.beta, fit.rho, fit.nu);
    double truth = sabr::impliedVol(F, 85, TS, ALPHA, BETA, RHO, NU);
    EXPECT_NEAR(interp, truth, 0.003);
}

TEST(sabr_atmLevelsPinnedForBetaOneAndBetaHalf) {
    // FormulaPinsTest: beta = 1 ATM Hagan is
    // alpha*(1 + T*(rho*nu*alpha/4 + (2 - 3rho^2) nu^2/24))
    // = 0.2*(1 + (-0.3*0.6*0.2/4 + (2 - 0.27)*0.36/24)) = 0.2*1.016950.
    EXPECT_NEAR(sabr::impliedVol(100, 100, 1.0, 0.20, 1.0, -0.30, 0.60),
                0.2033900, 1e-7);
    // beta = 0.5: alpha/f^0.5 * (1 + T*((1-b)^2 a^2/(24 f) + rho b nu a/(4 f^0.5)
    // + (2-3rho^2) nu^2/24)) = 0.2*(1 + 4.1667e-4 - 0.0045 + 0.02595).
    EXPECT_NEAR(sabr::impliedVol(100, 100, 1.0, 2.0, 0.5, -0.30, 0.60),
                0.2043733, 1e-7);
}

// ---- IncrementalGreeks (delta-gamma tick updates) -----------------------

namespace {
constexpr double IS = 100, IK = 105, IR = 0.04, IQ = 0.01, IVOL = 0.20, IT = 0.5;
}

TEST(incremental_smallMovesTrackTheFullRepriceToQuadraticAccuracy) {
    IncrementalGreeks inc;
    inc.reprice(OptionType::CALL, IS, IK, IR, IQ, IVOL, IT);
    // Anchor state reproduces the full pricer exactly.
    EXPECT_NEAR(inc.estimatedPrice(),
                bs::price(OptionType::CALL, IS, IK, IR, IQ, IVOL, IT), 1e-12);
    EXPECT_NEAR(inc.anchorSpot(), IS, 0.0);

    // A 0.3% move: Taylor error is O(dS^3 * speed) — far below a cent.
    double moved = IS * 1.003;
    inc.onTick(moved);
    double fullPrice = bs::price(OptionType::CALL, moved, IK, IR, IQ, IVOL, IT);
    double fullDelta = bs::delta(OptionType::CALL, moved, IK, IR, IQ, IVOL, IT);
    EXPECT_NEAR(inc.estimatedPrice(), fullPrice, 2e-4);
    EXPECT_NEAR(inc.estimatedDelta(), fullDelta, 2e-4);
    // Anchor Greeks are exposed for vega/theta risk.
    EXPECT_NEAR(inc.vega(), bs::vega(IS, IK, IR, IQ, IVOL, IT), 1e-12);
    EXPECT_TRUE(inc.gamma() > 0);
}

TEST(incremental_repriceSignalFiresOnDriftAndClearsOnReanchor) {
    IncrementalGreeks inc;
    inc.reprice(OptionType::PUT, IS, IK, IR, IQ, IVOL, IT);
    inc.onTick(IS * 1.002);
    EXPECT_TRUE(!inc.needsReprice(IS * 0.005));  // 0.2% < 0.5% drift budget
    inc.onTick(IS * 1.02);
    EXPECT_TRUE(inc.needsReprice(IS * 0.005));   // 2% blows the budget
    // Re-anchoring at the new spot clears the signal.
    inc.reprice(OptionType::PUT, IS * 1.02, IK, IR, IQ, IVOL, IT);
    EXPECT_TRUE(!inc.needsReprice(IS * 0.005));
}

QTEST_MAIN()
