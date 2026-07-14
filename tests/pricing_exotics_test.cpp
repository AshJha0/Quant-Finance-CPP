// Port of the Java ExoticOptionsTest: digital, touch and barrier pricing —
// exact parities against the vanilla pricer, limiting behavior, and Monte
// Carlo cross-checks of the reflection-principle formulas (with the
// discrete-monitoring bias of the simulation accounted for in the
// assertion direction). The MC helpers reproduce the Java test's
// java.util.Random(42)/Random(4242) streams bit-exactly via JavaRandom.
#include <algorithm>
#include <cmath>

#include "qtest.hpp"
#include "quantfinlib/pricing/barrier_option.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/pricing/digital_option.hpp"
#include "quantfinlib/pricing/java_random.hpp"
#include "quantfinlib/pricing/touch_option.hpp"

using quantfinlib::JavaRandom;
using quantfinlib::OptionType;
namespace bs = quantfinlib::black_scholes;
namespace dig = quantfinlib::digital_option;
namespace touch = quantfinlib::touch_option;
namespace bar = quantfinlib::barrier_option;

namespace {

constexpr double S = 1.0850;   // FX-style levels (EURUSD)
constexpr double R = 0.045;    // domestic (USD) rate
constexpr double Q = 0.030;    // foreign (EUR) rate = carry
constexpr double VOL = 0.09;
constexpr double T = 0.5;

// Deterministic GBM Monte Carlo (log-Euler is exact for GBM per step).
constexpr int PATHS = 60'000;
constexpr int STEPS = 400;

double mcHitProbability(double s0, double barrier, bool upper) {
    JavaRandom rng(42);
    double dt = T / STEPS;
    double drift = (R - Q - 0.5 * VOL * VOL) * dt;
    double diff = VOL * std::sqrt(dt);
    int hits = 0;
    for (int p = 0; p < PATHS; p++) {
        double s = s0;
        for (int i = 0; i < STEPS; i++) {
            s *= std::exp(drift + diff * rng.nextGaussian());
            if (upper ? s >= barrier : s <= barrier) {
                hits++;
                break;
            }
        }
    }
    return static_cast<double>(hits) / PATHS;
}

double mcDownAndOutCall(double s0, double strike, double barrier) {
    JavaRandom rng(4242);
    double dt = T / STEPS;
    double drift = (R - Q - 0.5 * VOL * VOL) * dt;
    double diff = VOL * std::sqrt(dt);
    double sum = 0;
    for (int p = 0; p < PATHS; p++) {
        double s = s0;
        bool knocked = false;
        for (int i = 0; i < STEPS; i++) {
            s *= std::exp(drift + diff * rng.nextGaussian());
            if (s <= barrier) {
                knocked = true;
                break;
            }
        }
        if (!knocked) {
            sum += std::max(0.0, s - strike);
        }
    }
    return std::exp(-R * T) * sum / PATHS;
}

}  // namespace

// ------------------------------------------------------------------
// Digitals
// ------------------------------------------------------------------

TEST(digital_paritiesHoldExactly) {
    double k = 1.10;
    double cashCall = dig::cashOrNothing(OptionType::CALL, S, k, R, Q, VOL, T, 1);
    double cashPut = dig::cashOrNothing(OptionType::PUT, S, k, R, Q, VOL, T, 1);
    // Call + put digitals pay 1 in every state: worth the discount factor.
    EXPECT_NEAR(cashCall + cashPut, std::exp(-R * T), 1e-12);

    double assetCall = dig::assetOrNothing(OptionType::CALL, S, k, R, Q, VOL, T);
    double assetPut = dig::assetOrNothing(OptionType::PUT, S, k, R, Q, VOL, T);
    EXPECT_NEAR(assetCall + assetPut, S * std::exp(-Q * T), 1e-12);

    // A vanilla is asset-or-nothing minus K cash-or-nothings.
    double vanilla = bs::price(OptionType::CALL, S, k, R, Q, VOL, T);
    EXPECT_NEAR(assetCall - k * cashCall, vanilla, 1e-12);
}

TEST(digital_expiryAndValidation) {
    EXPECT_NEAR(dig::cashOrNothing(OptionType::CALL, 1.10, 1.05, R, Q, VOL, 0, 5),
                5, 0.0);
    EXPECT_NEAR(dig::cashOrNothing(OptionType::CALL, 1.00, 1.05, R, Q, VOL, 0, 5),
                0, 0.0);
    EXPECT_NEAR(dig::assetOrNothing(OptionType::CALL, 1.10, 1.05, R, Q, VOL, 0),
                1.10, 0.0);
    EXPECT_THROWS(dig::cashOrNothing(OptionType::CALL, -1, 1, R, Q, VOL, T, 1));
}

// ------------------------------------------------------------------
// Touches
// ------------------------------------------------------------------

TEST(touch_propertiesAndComplement) {
    double upper = 1.12;
    double p = touch::hitProbability(S, upper, R, Q, VOL, T);
    EXPECT_TRUE(p > 0 && p < 1);
    // Barrier at spot: certain touch. Barrier far away: nearly none.
    EXPECT_NEAR(touch::hitProbability(S, S, R, Q, VOL, T), 1, 0.0);
    EXPECT_TRUE(touch::hitProbability(S, 2.5, R, Q, VOL, T) < 1e-9);
    // One-touch + no-touch = discounted payout (complement identity).
    double ot = touch::oneTouch(S, upper, R, Q, VOL, T, 100);
    double nt = touch::noTouch(S, upper, R, Q, VOL, T, 100);
    EXPECT_NEAR(ot + nt, 100 * std::exp(-R * T), 1e-9);
    // Longer expiry can only raise the hit probability.
    EXPECT_TRUE(touch::hitProbability(S, upper, R, Q, VOL, 1.0) > p);
    // Lower barrier branch.
    double pLow = touch::hitProbability(S, 1.05, R, Q, VOL, T);
    EXPECT_TRUE(pLow > 0 && pLow < 1);
}

TEST(touch_probabilityMatchesMonteCarlo) {
    double upper = 1.11;
    double closed = touch::hitProbability(S, upper, R, Q, VOL, T);
    double mc = mcHitProbability(S, upper, true);
    // Discrete monitoring misses intra-step touches: MC is biased LOW.
    EXPECT_TRUE(mc <= closed + 0.005);
    EXPECT_NEAR(mc, closed, 0.03);

    double lower = 1.06;
    double closedLow = touch::hitProbability(S, lower, R, Q, VOL, T);
    double mcLow = mcHitProbability(S, lower, false);
    EXPECT_NEAR(mcLow, closedLow, 0.03);
}

// ------------------------------------------------------------------
// Barriers
// ------------------------------------------------------------------

TEST(barrier_inOutParityReconstructsTheVanilla) {
    double k = 1.10;
    double h = 1.05;
    double vanilla = bs::price(OptionType::CALL, S, k, R, Q, VOL, T);
    double ki = bar::downAndInCall(S, k, h, R, Q, VOL, T);
    double ko = bar::downAndOutCall(S, k, h, R, Q, VOL, T);
    EXPECT_NEAR(ki + ko, vanilla, 1e-12);
    EXPECT_TRUE(ki > 0 && ko > 0);

    double hp = 1.13;
    double kp = 1.08;
    double vanillaPut = bs::price(OptionType::PUT, S, kp, R, Q, VOL, T);
    EXPECT_NEAR(bar::upAndInPut(S, kp, hp, R, Q, VOL, T)
                        + bar::upAndOutPut(S, kp, hp, R, Q, VOL, T),
                vanillaPut, 1e-12);
}

TEST(barrier_limitingBarriersRecoverVanillaAndZero) {
    double k = 1.10;
    double vanilla = bs::price(OptionType::CALL, S, k, R, Q, VOL, T);
    // A barrier miles below spot never knocks: KO ~ vanilla, KI ~ 0.
    EXPECT_NEAR(bar::downAndOutCall(S, k, 0.40, R, Q, VOL, T), vanilla, 1e-9);
    EXPECT_TRUE(bar::downAndInCall(S, k, 0.40, R, Q, VOL, T) < 1e-9);
    // A barrier just below spot almost surely knocks: KO ~ 0.
    EXPECT_TRUE(bar::downAndOutCall(S, k, S - 1e-4, R, Q, VOL, T) < 0.002);
}

TEST(barrier_priceMatchesMonteCarlo) {
    double k = 1.10;
    double h = 1.05;
    double closed = bar::downAndOutCall(S, k, h, R, Q, VOL, T);
    double mc = mcDownAndOutCall(S, k, h);
    // Discrete monitoring misses knocks: MC knock-OUT is biased HIGH.
    EXPECT_TRUE(mc >= closed - 0.0005);
    EXPECT_NEAR(mc, closed, 0.002);
}

TEST(barrier_reverseAndBreachedBarriersAreRejected) {
    // Breached down barrier.
    EXPECT_THROWS(bar::downAndOutCall(S, 1.10, 1.09, R, Q, VOL, T));
    // Reverse: H > K on a call.
    EXPECT_THROWS(bar::downAndOutCall(1.20, 1.02, 1.05, R, Q, VOL, T));
    // Breached up barrier.
    EXPECT_THROWS(bar::upAndOutPut(S, 1.08, 1.05, R, Q, VOL, T));
    // Reverse: H < K on a put.
    EXPECT_THROWS(bar::upAndOutPut(1.00, 1.15, 1.10, R, Q, VOL, T));
}

QTEST_MAIN()
