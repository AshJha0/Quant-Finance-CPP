// Port of fx.FxVolSurfaceTest (Java): delta-quoted FX smile — strike
// solving round-trips the target delta, the surface reproduces the broker
// quote identities (RR/BF), the delta-neutral straddle is really delta-
// neutral, and premium-adjusted solving differs from unadjusted in the
// documented direction. Pins and tolerances unchanged.
#include <cmath>

#include "qtest.hpp"
#include "quantfinlib/fx/fx_vol_surface.hpp"
#include "quantfinlib/util/math_utils.hpp"

using quantfinlib::FxVolSurface;
namespace math_utils = quantfinlib::math_utils;

namespace {

constexpr double F = 1.0850;    // EURUSD 3M forward
constexpr double T = 0.25;
constexpr double ATM = 0.078;   // 7.8 vols
constexpr double RR25 = -0.010;  // EURUSD-style: puts over calls
constexpr double BF25 = 0.0022;

FxVolSurface surface() {
    return FxVolSurface::builder()
        .add(T, F, ATM, RR25, BF25)
        .add(1.0, 1.0920, 0.083, -0.012, 0.0030, -0.020, 0.0095)
        .build();
}

/** Forward delta of a call/put at a strike under Black (forward measure). */
double forwardDelta(double f, double k, double vol, double t, bool call) {
    double sv = vol * std::sqrt(t);
    double d1 = std::log(f / k) / sv + 0.5 * sv;
    return call ? math_utils::normCdf(d1) : -math_utils::normCdf(-d1);
}

}  // namespace

TEST(strikeForDeltaRoundTripsTheTargetDelta) {
    double k25c = FxVolSurface::strikeForDelta(F, ATM, T, 0.25, true, false);
    double k25p = FxVolSurface::strikeForDelta(F, ATM, T, -0.25, false, false);
    EXPECT_NEAR(0.25, forwardDelta(F, k25c, ATM, T, true), 1e-6);
    EXPECT_NEAR(-0.25, forwardDelta(F, k25p, ATM, T, false), 1e-6);
    EXPECT_TRUE(k25c > F);
    EXPECT_TRUE(k25p < F);
}

TEST(dnsStrikeIsDeltaNeutral) {
    double k = FxVolSurface::dnsStrike(F, ATM, T, false);
    EXPECT_NEAR(F * std::exp(0.5 * ATM * ATM * T), k, 1e-12);
    double dc = forwardDelta(F, k, ATM, T, true);
    double dp = forwardDelta(F, k, ATM, T, false);
    EXPECT_NEAR(0, dc + dp, 1e-7);
}

TEST(surfaceReproducesBrokerQuoteIdentities) {
    FxVolSurface s = surface();
    FxVolSurface::SmilePillar p = s.pillar(0);
    EXPECT_TRUE(p.strikes.size() == 3);
    double v25p = p.vols[0];
    double vAtm = p.vols[1];
    double v25c = p.vols[2];
    EXPECT_NEAR(RR25, v25c - v25p, 1e-12);
    EXPECT_NEAR(BF25, (v25c + v25p) / 2 - vAtm, 1e-12);
    EXPECT_NEAR(ATM, vAtm, 1e-12);
    EXPECT_NEAR(v25c, s.vol(T, p.strikes[2]), 1e-10);
    EXPECT_TRUE(v25p > v25c);
}

TEST(tenDeltaWingsExtendTheSmile) {
    FxVolSurface s = surface();
    FxVolSurface::SmilePillar p = s.pillar(1);
    EXPECT_TRUE(p.strikes.size() == 5);
    EXPECT_NEAR(p.vols[0], s.vol(1.0, p.strikes[0] * 0.90), 1e-10);
    EXPECT_NEAR(p.vols[4], s.vol(1.0, p.strikes[4] * 1.10), 1e-10);
}

TEST(timeInterpolationIsLinearInTotalVariance) {
    FxVolSurface s = surface();
    double tMid = 0.5;
    double k = s.forwardAt(tMid);
    double vLo = s.vol(T, s.forwardAt(T));
    double vHi = s.vol(1.0, s.forwardAt(1.0));
    double wExpected = vLo * vLo * T + (vHi * vHi * 1.0 - vLo * vLo * T) * (tMid - T) / (1.0 - T);
    EXPECT_NEAR(std::sqrt(wExpected / tMid), s.vol(tMid, k), 1e-12);
    EXPECT_NEAR(s.vol(T, k), s.vol(0.05, k), 1e-12);
}

TEST(premiumAdjustedSolvingHitsThePaDelta) {
    double sv = ATM * std::sqrt(T);
    double k = FxVolSurface::strikeForDelta(F, ATM, T, 0.25, true, true);
    double d2 = std::log(F / k) / sv - 0.5 * sv;
    EXPECT_NEAR(0.25, (k / F) * math_utils::normCdf(d2), 1e-9);
    double kUnadj = FxVolSurface::strikeForDelta(F, ATM, T, 0.25, true, false);
    EXPECT_TRUE(k < kUnadj);
    EXPECT_TRUE(FxVolSurface::dnsStrike(F, ATM, T, true) < F);
    double kp = FxVolSurface::strikeForDelta(F, ATM, T, -0.25, false, true);
    double d2p = std::log(F / kp) / sv - 0.5 * sv;
    EXPECT_NEAR(-0.25, -(kp / F) * math_utils::normCdf(-d2p), 1e-9);
}

TEST(validationRejectsBadInput) {
    EXPECT_THROWS(FxVolSurface::builder().build());
    EXPECT_THROWS(FxVolSurface::builder().add(0, F, ATM, 0, 0));
    EXPECT_THROWS(FxVolSurface::strikeForDelta(F, ATM, T, 1.5, true, false));
    EXPECT_THROWS(surface().vol(T, -1));
    EXPECT_THROWS(FxVolSurface::strikeForDelta(F, ATM, T, 0.999, true, true));
}

TEST(addRejectsRrBfCombinationThatProducesNonPositiveWingVol) {
    // atm + bf25 - rr25/2 goes to zero/negative for an aggressive combination.
    EXPECT_THROWS(FxVolSurface::builder().add(T, F, 0.05, 0.20, -0.03));
}

TEST(strikeForDeltaAcceptsPaDeltaNearTheTrueContinuousPeak) {
    // A delta right at the edge of the coarse 200-point grid's discretely-
    // sampled maximum must still resolve -- the golden-section refinement
    // closes the gap to the true continuous peak.
    double sv = ATM * std::sqrt(T);
    double lo = F * std::exp(-8 * sv);
    double hi = F * std::exp(8 * sv);
    double gridPeakVal = -1;
    for (int i = 0; i <= 200; i++) {
        double k = lo * std::pow(hi / lo, i / 200.0);
        double sv2 = ATM * std::sqrt(T);
        double d2 = std::log(F / k) / sv2 - 0.5 * sv2;
        double v = (k / F) * math_utils::normCdf(d2);
        if (v > gridPeakVal) {
            gridPeakVal = v;
        }
    }
    double nearPeakDelta = gridPeakVal - 1e-9;
    double k = FxVolSurface::strikeForDelta(F, ATM, T, nearPeakDelta, true, true);
    EXPECT_TRUE(std::isfinite(k) && k > 0);
}

QTEST_MAIN()
