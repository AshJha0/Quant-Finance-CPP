// Ports of the Java pricing tests for the surface/market layer:
// VolSurfaceTest, DividendScheduleTest, VannaVolgaTest (minus the
// FxVolSurface integration leg — the fx domain is not ported; the
// five-pillar reduction check is kept), and PricingTest (triangular
// arbitrage, forward curve, fair value engine).
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/pricing/dividend_schedule.hpp"
#include "quantfinlib/pricing/fair_value_engine.hpp"
#include "quantfinlib/pricing/forward_curve.hpp"
#include "quantfinlib/pricing/triangular_arbitrage.hpp"
#include "quantfinlib/pricing/vanna_volga.hpp"
#include "quantfinlib/pricing/vol_surface.hpp"

using quantfinlib::DividendSchedule;
using quantfinlib::FairValueEngine;
using quantfinlib::ForwardCurve;
using quantfinlib::OptionType;
using quantfinlib::VannaVolga;
using quantfinlib::VolSurface;
namespace bs = quantfinlib::black_scholes;
namespace tri = quantfinlib::triangular_arbitrage;
using tri::Quote;

// ---- VolSurface ---------------------------------------------------------

namespace {

VolSurface makeSurface() {
    return VolSurface::builder()
            .add(0.25, 90, 0.25).add(0.25, 100, 0.20).add(0.25, 110, 0.22)
            .add(1.00, 90, 0.28).add(1.00, 100, 0.24).add(1.00, 110, 0.25)
            .build();
}

}  // namespace

TEST(surface_recoversPillarQuotesExactly) {
    VolSurface surface = makeSurface();
    EXPECT_NEAR(surface.vol(0.25, 100), 0.20, 1e-12);
    EXPECT_NEAR(surface.vol(1.00, 90), 0.28, 1e-12);
    EXPECT_TRUE(surface.expiries().size() == 2);
    EXPECT_TRUE(surface.strikes(0.25).size() == 3);
}

TEST(surface_interpolatesLinearlyAcrossStrikes) {
    VolSurface surface = makeSurface();
    // Midway between 90 (0.25) and 100 (0.20).
    EXPECT_NEAR(surface.vol(0.25, 95), 0.225, 1e-12);
    // Flat extrapolation beyond the wings.
    EXPECT_NEAR(surface.vol(0.25, 80), 0.25, 1e-12);
    EXPECT_NEAR(surface.vol(0.25, 130), 0.22, 1e-12);
}

TEST(surface_interpolatesInTotalVarianceAcrossExpiries) {
    VolSurface surface = makeSurface();
    // K=100: w1 = 0.04*0.25 = 0.01, w2 = 0.0576*1.0 = 0.0576.
    // t=0.625 (halfway): w = 0.0338 -> vol = sqrt(0.0338/0.625).
    EXPECT_NEAR(surface.vol(0.625, 100), std::sqrt(0.0338 / 0.625), 1e-9);
    // Flat vol extrapolation outside the pillar range.
    EXPECT_NEAR(surface.vol(0.10, 100), 0.20, 1e-12);
    EXPECT_NEAR(surface.vol(3.00, 100), 0.24, 1e-12);
}

TEST(surface_smileShapeIsPreserved) {
    VolSurface surface = makeSurface();
    // Put skew: downside strikes carry higher vol at every expiry.
    EXPECT_TRUE(surface.vol(1.0, 90) > surface.vol(1.0, 100));
    EXPECT_TRUE(surface.vol(0.625, 90) > surface.vol(0.625, 100));
    EXPECT_TRUE(surface.skew(1.0, 90, 100) < 0);
}

TEST(surface_buildsFromMarketPricesViaImpliedVol) {
    double spot = 100, rate = 0.03, carry = 0.01;
    double p1 = bs::price(OptionType::CALL, spot, 100, rate, carry, 0.30, 0.5);
    double p2 = bs::price(OptionType::PUT, spot, 90, rate, carry, 0.35, 0.5);

    VolSurface fromPrices = VolSurface::builder()
            .addFromPrice(OptionType::CALL, p1, spot, 100, rate, carry, 0.5)
            .addFromPrice(OptionType::PUT, p2, spot, 90, rate, carry, 0.5)
            .build();

    EXPECT_NEAR(fromPrices.vol(0.5, 100), 0.30, 1e-6);
    EXPECT_NEAR(fromPrices.vol(0.5, 90), 0.35, 1e-6);
}

TEST(surface_pricesWithTheSurfaceVol) {
    VolSurface surface = makeSurface();
    double expected = bs::price(OptionType::CALL, 100, 95,
                                0.02, 0, surface.vol(0.5, 95), 0.5);
    EXPECT_NEAR(surface.price(OptionType::CALL, 100, 95, 0.02, 0, 0.5),
                expected, 1e-12);
}

TEST(surface_validatesInputs) {
    VolSurface surface = makeSurface();
    EXPECT_THROWS(VolSurface::builder().build());            // runtime_error
    EXPECT_THROWS(VolSurface::builder().add(-1, 100, 0.2));  // invalid_argument
    EXPECT_THROWS(surface.strikes(0.33));
}

// ---- DividendSchedule ----------------------------------------------------

namespace {
constexpr double DS = 100, DR = 0.04, DVOL = 0.25, DT = 1.0;

DividendSchedule quarterlyDollar() {
    // Quarterly $1 dividends.
    return DividendSchedule::of({0.25, 0.50, 0.75, 1.00}, {1, 1, 1, 1});
}
}  // namespace

TEST(divs_presentValueDiscountsAndFiltersByHorizon) {
    DividendSchedule divs = quarterlyDollar();
    double expected = std::exp(-DR * 0.25) + std::exp(-DR * 0.50);
    // Only ex-dates on/before the horizon count.
    EXPECT_NEAR(divs.presentValue(DR, 0.6), expected, 1e-12);
    EXPECT_NEAR(divs.presentValue(DR, 0.1), 0, 1e-12);
    EXPECT_TRUE(divs.count() == 4);
    EXPECT_NEAR(DividendSchedule::NONE().presentValue(DR, 10), 0, 1e-12);
}

TEST(divs_forwardDropsByDividendsAndBorrow) {
    DividendSchedule divs = quarterlyDollar();
    double noDivForward = DS * std::exp(DR * DT);
    double withDivs = divs.forward(DS, DR, 0, DT);
    EXPECT_TRUE(withDivs < noDivForward);
    EXPECT_NEAR(withDivs, divs.adjustedSpot(DS, DR, DT) * std::exp(DR * DT), 1e-12);
    // Borrow fee acts like extra yield: forward drops further.
    EXPECT_TRUE(divs.forward(DS, DR, 0.02, DT) < withDivs);
    // No dividends, no borrow: the plain cost-of-carry forward.
    EXPECT_NEAR(DividendSchedule::NONE().forward(DS, DR, 0, DT), noDivForward, 1e-12);
}

TEST(divs_optionPricesMoveInTheDividendDirection) {
    DividendSchedule divs = quarterlyDollar();
    double plainCall = bs::price(OptionType::CALL, DS, 100, DR, 0, DVOL, DT);
    double plainPut = bs::price(OptionType::PUT, DS, 100, DR, 0, DVOL, DT);
    double divCall = divs.europeanPrice(OptionType::CALL, DS, 100, DR, 0, DVOL, DT);
    double divPut = divs.europeanPrice(OptionType::PUT, DS, 100, DR, 0, DVOL, DT);
    // Dividends leak value out of the diffusion: calls down, puts up.
    EXPECT_TRUE(divCall < plainCall);
    EXPECT_TRUE(divPut > plainPut);
    // Empty schedule reproduces Black-Scholes exactly.
    EXPECT_NEAR(DividendSchedule::NONE().europeanPrice(
                        OptionType::CALL, DS, 100, DR, 0, DVOL, DT),
                plainCall, 1e-12);
    // Put-call parity on the ADJUSTED spot (the escrowed model's parity).
    double adjusted = divs.adjustedSpot(DS, DR, DT);
    EXPECT_NEAR(divCall - divPut, adjusted - 100 * std::exp(-DR * DT), 1e-10);
}

TEST(divs_validationRejectsBadSchedules) {
    EXPECT_THROWS(DividendSchedule::of({1}, {1, 2}));
    EXPECT_THROWS(DividendSchedule::of({0}, {1}));
    EXPECT_THROWS(DividendSchedule::of({0.5, 0.25}, {1, 1}));
    EXPECT_THROWS(DividendSchedule::of({0.5}, {-1}));
    // Dividend PV swamping spot must be reported, not priced.
    DividendSchedule huge = DividendSchedule::of({0.5}, {200});
    EXPECT_THROWS(huge.adjustedSpot(DS, DR, DT));
}

// ---- VannaVolga -----------------------------------------------------------

namespace {
constexpr double VS = 1.0850, VR = 0.045, VQ = 0.030, VT = 0.5;
// A negative-RR (put wing over call wing) EURUSD-style smile.
const std::vector<double> V_STRIKES{1.04, 1.09, 1.14};
const std::vector<double> V_VOLS{0.098, 0.090, 0.094};

VannaVolga makeVv() {
    return VannaVolga(V_STRIKES, V_VOLS, VR, VQ, VT);
}
}  // namespace

TEST(vv_pillarStrikesRecoverTheirMarketVolsExactly) {
    VannaVolga vv = makeVv();
    for (int i = 0; i < 3; i++) {
        EXPECT_NEAR(vv.impliedVol(VS, V_STRIKES[i]), V_VOLS[i], 1e-6);
    }
}

TEST(vv_pillarPricesMatchTheMarketPrices) {
    VannaVolga vv = makeVv();
    for (int i = 0; i < 3; i++) {
        double market = bs::price(OptionType::CALL, VS, V_STRIKES[i], VR, VQ,
                                  V_VOLS[i], VT);
        EXPECT_NEAR(vv.price(OptionType::CALL, VS, V_STRIKES[i]), market, 1e-12);
    }
}

TEST(vv_smileInterpolatesSmoothlyBetweenPillars) {
    VannaVolga vv = makeVv();
    // Between ATM and the call wing the vol must sit between their levels
    // (the log-quadratic weights cannot overshoot on this smile shape).
    double mid = vv.impliedVol(VS, 1.115);
    EXPECT_TRUE(mid > std::min(V_VOLS[1], V_VOLS[2]) - 1e-4
            && mid < std::max(V_VOLS[1], V_VOLS[2]) + 1e-4);
    // The smile is not flat: the adjustment is really doing something.
    EXPECT_TRUE(std::fabs(mid - V_VOLS[1]) > 1e-4);
    // Wings continue outward without collapsing.
    EXPECT_TRUE(vv.impliedVol(VS, 1.00) > V_VOLS[1]);
}

TEST(vv_putPricesAreParityConsistent) {
    VannaVolga vv = makeVv();
    double k = 1.115;
    double call = vv.price(OptionType::CALL, VS, k);
    double put = vv.price(OptionType::PUT, VS, k);
    // Same smile adjustment on both sides: put-call parity in the adjusted
    // prices (parity holds for the flat-vol legs and the pillar-hedge
    // adjustment is type-independent).
    double parity = VS * std::exp(-VQ * VT) - k * std::exp(-VR * VT);
    EXPECT_NEAR(call - put, parity, 1e-10);
}

TEST(vv_fivePillarOverloadReducesToTheTwentyFiveDeltaTriple) {
    // The FxVolSurface integration leg of the Java test needs the fx
    // domain (not ported); the five-pillar reduction is pure VannaVolga.
    std::vector<double> five{1.02, 1.05, 1.09, 1.13, 1.16};
    std::vector<double> fiveVols{0.101, 0.097, 0.090, 0.093, 0.099};
    VannaVolga reduced = VannaVolga::ofPillars(five, fiveVols, VR, VQ, VT);
    EXPECT_NEAR(reduced.impliedVol(VS, five[2]), fiveVols[2], 1e-6);
}

TEST(vv_validationRejectsBadPillars) {
    VannaVolga vv = makeVv();
    EXPECT_THROWS((VannaVolga(std::vector<double>{1, 2},
                              std::vector<double>{0.1, 0.1}, VR, VQ, VT)));
    EXPECT_THROWS((VannaVolga(std::vector<double>{1.1, 1.0, 1.2}, V_VOLS,
                              VR, VQ, VT)));
    EXPECT_THROWS((VannaVolga(V_STRIKES, std::vector<double>{0.1, -0.1, 0.1},
                              VR, VQ, VT)));
    EXPECT_THROWS((VannaVolga(V_STRIKES, V_VOLS, VR, VQ, 0)));
    EXPECT_THROWS(vv.price(OptionType::CALL, VS, -1));
}

// ---- TriangularArbitrage / ForwardCurve / FairValueEngine (PricingTest) --

TEST(tri_consistentTriangleShowsNoArbitrage) {
    Quote eurusd(1.1000, 1.1002);
    Quote usdjpy(150.00, 150.02);
    Quote eurjpy(165.00, 165.06);  // consistent with 1.1001 * 150.01 = 165.03
    EXPECT_TRUE(!tri::exists(eurusd, usdjpy, eurjpy, 0.0));
    EXPECT_TRUE(tri::arbitrageBps(eurusd, usdjpy, eurjpy) < 0);
}

TEST(tri_dislocatedCrossShowsExecutableArbitrage) {
    Quote eurusd(1.1000, 1.1002);
    Quote usdjpy(150.00, 150.02);
    // EURJPY bid far above synthetic ask (1.1002 * 150.02 = 165.05):
    // sell direct, buy synthetic.
    Quote eurjpy(165.40, 165.45);
    double bps = tri::arbitrageBps(eurusd, usdjpy, eurjpy);
    EXPECT_TRUE(bps > 15);
    EXPECT_TRUE(tri::exists(eurusd, usdjpy, eurjpy, 5));
    EXPECT_NEAR(tri::impliedCrossMid(eurusd, usdjpy), 1.1001 * 150.01, 1e-9);
}

TEST(tri_crossedQuoteIsRejected) {
    EXPECT_THROWS((Quote(1.10, 1.09)));
}

TEST(curve_forwardCurveInterpolatesAndExtrapolates) {
    ForwardCurve curve(1.1000);
    curve.addPoint(0.25, 1.1050).addPoint(1.0, 1.1200);

    EXPECT_NEAR(curve.forward(0), 1.1000, 1e-12);
    EXPECT_NEAR(curve.forward(0.25), 1.1050, 1e-12);
    // Midway between 0.25y and 1.0y pillars.
    double halfway = 1.1050 + (1.1200 - 1.1050) * (0.625 - 0.25) / 0.75;
    EXPECT_NEAR(curve.forward(0.625), halfway, 1e-12);
    // Extrapolation continues the last slope.
    double slope = (1.1200 - 1.1050) / 0.75;
    EXPECT_NEAR(curve.forward(1.5), 1.1200 + slope * 0.5, 1e-9);
    EXPECT_NEAR(curve.forwardPoints(1.0), 1.1200 - 1.1000, 1e-12);
}

TEST(curve_coveredInterestParityChecks) {
    double spot = 1.10, rd = 0.05, rf = 0.02, t = 1.0;
    double fair = ForwardCurve::theoreticalForward(spot, rd, rf, t);
    EXPECT_NEAR(fair, 1.10 * 1.05 / 1.02, 1e-12);

    ForwardCurve fairCurve(spot);
    fairCurve.addPoint(t, fair);
    EXPECT_NEAR(fairCurve.mispricingBps(t, rd, rf), 0, 1e-9);
    // Implied differential recovers ~ln(F/S)/t.
    EXPECT_NEAR(fairCurve.impliedRateDifferential(t), std::log(fair / spot), 1e-12);

    ForwardCurve richCurve(spot);
    richCurve.addPoint(t, fair * 1.001);
    EXPECT_NEAR(richCurve.mispricingBps(t, rd, rf), 10, 0.05);
}

TEST(fve_micropriceAndLatencyAdjustedFair) {
    // Size-weighted: I = 90/100 -> 0.9*101 + 0.1*99 = 100.8.
    EXPECT_NEAR(FairValueEngine::microprice(99, 101, 90, 10), 100.8, 1e-12);

    FairValueEngine engine(64, 1'000'000'000LL);
    // Mid rising by 1.0 per second: quotes 100ms apart, +0.1 each.
    for (int i = 0; i < 10; i++) {
        double mid = 100 + 0.1 * i;
        engine.onQuote(mid - 0.01, mid + 0.01, 500, 500, i * 100'000'000LL);
    }
    EXPECT_NEAR(engine.driftPerSecond(), 1.0, 0.01);
    // Balanced book: microprice = last mid; 50ms latency adds ~0.05 of drift.
    EXPECT_NEAR(engine.microprice(), 100.9, 1e-9);
    EXPECT_NEAR(engine.latencyAdjustedFair(50'000'000LL), 100.95, 0.005);
}

QTEST_MAIN()
