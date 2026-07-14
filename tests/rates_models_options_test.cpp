// Port of rates.SwapPricerTest, rates.RatesOptionsTest, the swap pins of
// markets.AssetClassRoundTest, and the short-rate-model pins of
// pricing.MarketRiskPricingTest (Java). Pins and tolerances unchanged.
#include <cmath>
#include <limits>

#include "qtest.hpp"
#include "quantfinlib/rates/rates_options.hpp"
#include "quantfinlib/rates/short_rate_models.hpp"
#include "quantfinlib/rates/swap_pricer.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

using quantfinlib::YieldCurve;
namespace swap_pricer = quantfinlib::swap_pricer;
namespace rates_options = quantfinlib::rates_options;
namespace short_rate_models = quantfinlib::short_rate_models;

namespace {

YieldCurve flat5() {
    std::vector<double> t = {1, 2, 3, 4, 5, 7, 10};
    std::vector<double> z(t.size(), 0.05);
    return YieldCurve::ofZeroRates(t, z);
}

YieldCurve flat5Ten() {   // RatesOptionsTest's flat5(): pillars 1..10
    std::vector<double> tenors(10);
    std::vector<double> zeros(10, 0.05);
    for (int i = 0; i < 10; i++) {
        tenors[i] = i + 1;
    }
    return YieldCurve::ofZeroRates(tenors, zeros);
}

}  // namespace

// ------------------------------------------------------------ SwapPricer

TEST(payerPvIsExactlyAntisymmetricAroundPar) {
    // payerPv(K) = annuity * (par - K): equal strikes above and below
    // par give PVs of equal size and opposite sign.
    YieldCurve c = flat5();
    double par = swap_pricer::parRate(c, 5);
    double d = 0.01;
    double below = swap_pricer::payerPv(c, 5, par - d);
    double above = swap_pricer::payerPv(c, 5, par + d);
    EXPECT_NEAR(above, -below, 1e-15);  // linear in K: exact antisymmetry
    EXPECT_NEAR(below, swap_pricer::annuity(c, 5) * d, 1e-15);
    EXPECT_TRUE(below > 0);   // paying below par is an asset to the payer
    EXPECT_TRUE(above < 0);   // paying above par is a liability to the payer
}

TEST(annuityOnAFlatCurveIsTheHandGeometricSum) {
    // Flat 5% cc: DF(i) = e^{-0.05 i}, so the 5y annual annuity is
    // e^{-0.05} + e^{-0.10} + e^{-0.15} + e^{-0.20} + e^{-0.25}.
    YieldCurve c = flat5();
    double expected = 0;
    for (int i = 1; i <= 5; i++) {
        expected += std::exp(-0.05 * i);
    }
    EXPECT_NEAR(swap_pricer::annuity(c, 5), expected, 1e-12);
    EXPECT_TRUE(swap_pricer::annuity(c, 1) > 0);
    // Longer tenor adds strictly positive discount factors.
    EXPECT_TRUE(swap_pricer::annuity(c, 10) > swap_pricer::annuity(c, 5));
}

TEST(payerPvFallsStrictlyAsTheFixedRateRises) {
    YieldCurve c = flat5();
    double prev = std::numeric_limits<double>::infinity();
    for (double k = 0.01; k <= 0.09; k += 0.02) {
        double pv = swap_pricer::payerPv(c, 5, k);
        EXPECT_TRUE(pv < prev);   // paying a higher fixed rate must cost more
        prev = pv;
    }
}

TEST(swapGatesRefuseNonsense) {
    YieldCurve c = flat5();
    EXPECT_THROWS(swap_pricer::annuity(c, 0));
    EXPECT_THROWS(swap_pricer::parRate(c, -1));
    EXPECT_THROWS(swap_pricer::payerPv(c, 5, std::nan("")));
    EXPECT_THROWS(swap_pricer::payerPv(c, 0, 0.05));
}

// Port of AssetClassRoundTest.parSwapPricesToZeroAndMatchesRatesOptions.
TEST(parSwapPricesToZeroAndMatchesRatesOptions) {
    YieldCurve c = flat5();
    double par = swap_pricer::parRate(c, 5);
    // A swap struck at par is worth exactly zero.
    EXPECT_NEAR(swap_pricer::payerPv(c, 5, par), 0.0, 1e-12);
    // Same object RatesOptions computes as the 0-into-5y forward rate.
    EXPECT_NEAR(rates_options::forwardSwapRate(c, 0, 5), par, 1e-12);
    // Flat cc curve: par = the simple annual forward.
    EXPECT_NEAR(par, std::exp(0.05) - 1, 1e-12);
    // Below-par fixed rate favors the payer.
    EXPECT_TRUE(swap_pricer::payerPv(c, 5, par - 0.01) > 0);
}

// Port of AssetClassRoundTest.dv01IsAnnuityTimesTheParRateSensitivity.
TEST(dv01IsAnnuityTimesTheParRateSensitivity) {
    // A 1bp bump to the CC ZERO curve moves the SIMPLE par rate by e^z bp
    // (d(e^z - 1)/dz), so the payer DV01 on a flat 5% curve is
    // annuity * e^0.05 * 1bp — the naive "annuity * 1bp" is the
    // sensitivity to the PAR rate, a different derivative.
    YieldCurve c = flat5();
    double par = swap_pricer::parRate(c, 5);
    double dv01 = swap_pricer::dv01(c, 5, par);
    EXPECT_TRUE(dv01 > 0);   // rates up helps the fixed payer
    EXPECT_NEAR(dv01, swap_pricer::annuity(c, 5) * 1e-4 * std::exp(0.05),
                swap_pricer::annuity(c, 5) * 1e-4 * 0.01);
}

// ---------------------------------------------------------- RatesOptions

TEST(flatCurveForwardSwapRateIsTheSimpleForward) {
    // Flat cc curve: every annual simple forward is e^0.05 - 1, and a
    // par swap rate over identical forwards equals that forward.
    YieldCurve c = flat5Ten();
    double expected = std::exp(0.05) - 1;
    EXPECT_NEAR(rates_options::forwardSwapRate(c, 1, 5), expected, 1e-12);
    EXPECT_NEAR(rates_options::forwardSwapRate(c, 0, 10), expected, 1e-12);
}

TEST(swaptionPutCallParityIsExact) {
    YieldCurve c = flat5Ten();
    double strike = 0.04;
    double payer = rates_options::swaption(c, 1, 5, strike, 0.25, true);
    double receiver = rates_options::swaption(c, 1, 5, strike, 0.25, false);
    double annuity = rates_options::annuity(c, 1, 5);
    double fsr = rates_options::forwardSwapRate(c, 1, 5);
    // Payer - receiver must equal the forward swap PV, any vol.
    EXPECT_NEAR(payer - receiver, annuity * (fsr - strike), 1e-12);
    EXPECT_TRUE(payer > 0 && receiver > 0);

    // At-the-money forward: payer and receiver are worth the same.
    double atmPayer = rates_options::swaption(c, 1, 5, fsr, 0.25, true);
    double atmReceiver = rates_options::swaption(c, 1, 5, fsr, 0.25, false);
    EXPECT_NEAR(atmPayer, atmReceiver, 1e-12);
}

TEST(capMinusFloorIsTheSwapPv) {
    YieldCurve c = flat5Ten();
    double strike = 0.04;
    double cap = rates_options::cap(c, 5, strike, 0.30);
    double floor = rates_options::floor(c, 5, strike, 0.30);
    double swapPv = 0;
    for (int i = 1; i <= 5; i++) {
        double dfPay = c.discountFactor(i);
        double fwd = c.discountFactor(i - 1) / dfPay - 1;
        swapPv += dfPay * (fwd - strike);
    }
    EXPECT_NEAR(cap - floor, swapPv, 1e-12);  // cap - floor = swap, any vol
    // Longer caps contain more caplets: strictly more valuable.
    EXPECT_TRUE(rates_options::cap(c, 7, strike, 0.30) > cap);
}

TEST(ratesOptionsGates) {
    YieldCurve c = flat5Ten();
    EXPECT_THROWS(rates_options::annuity(c, 1, 0));
    EXPECT_THROWS(rates_options::annuity(c, -1, 5));
    EXPECT_THROWS(rates_options::swaption(c, 1, 5, 0, 0.25, true));
    EXPECT_THROWS(rates_options::swaption(c, 1, 5, 0.04, 0, true));
    EXPECT_THROWS(rates_options::cap(c, 0, 0.04, 0.30));
}

// ------------------------------------------------------ short-rate models
// Port of MarketRiskPricingTest.vasicekAndCirBondsBehaveLikeBonds and
// hullWhiteRepricesTodaysCurveByConstruction.

TEST(vasicekAndCirBondsBehaveLikeBonds) {
    // sigma = 0, r = b: the rate never moves -> P = e^{-bT} exactly.
    EXPECT_NEAR(short_rate_models::vasicekBond(0.03, 0.5, 0.03, 0, 5),
                std::exp(-0.03 * 5), 1e-12);
    // Convexity: with vol, the Gaussian rate makes the bond WORTH MORE
    // (Jensen on e^{-int r}).
    EXPECT_TRUE(short_rate_models::vasicekBond(0.03, 0.5, 0.03, 0.02, 5)
                > std::exp(-0.03 * 5));
    // Both decrease in the short rate; both price below par for r > 0.
    EXPECT_TRUE(short_rate_models::vasicekBond(0.05, 0.5, 0.03, 0.01, 5)
                < short_rate_models::vasicekBond(0.02, 0.5, 0.03, 0.01, 5));
    EXPECT_TRUE(short_rate_models::cirBond(0.05, 0.5, 0.03, 0.1, 5)
                < short_rate_models::cirBond(0.02, 0.5, 0.03, 0.1, 5));
    EXPECT_TRUE(short_rate_models::cirBond(0.03, 0.5, 0.03, 0.1, 5) < 1);
    // Yields recover the log-price.
    EXPECT_NEAR(short_rate_models::vasicekYield(0.03, 0.5, 0.03, 0, 5), 0.03, 1e-12);
    // Feller: 2ab vs sigma^2.
    EXPECT_TRUE(short_rate_models::cirFeller(0.5, 0.03, 0.1) > 1);
    EXPECT_TRUE(short_rate_models::cirFeller(0.1, 0.02, 0.2) < 1);
    // Exact Vasicek step is mean-reverting in expectation (z = 0).
    EXPECT_NEAR(short_rate_models::vasicekStep(0.05, 0.5, 0.03, 0.01, 1.0, 0),
                0.03 + (0.05 - 0.03) * std::exp(-0.5), 1e-12);
    // CIR full truncation never sources vol from a negative rate.
    double stepped = short_rate_models::cirStep(-0.01, 0.5, 0.03, 0.1, 1.0 / 252, 3.0);
    EXPECT_TRUE(std::isfinite(stepped));  // no sqrt(negative)
    EXPECT_THROWS(short_rate_models::vasicekBond(0.03, 0, 0.03, 0.01, 5));
}

TEST(hullWhiteRepricesTodaysCurveByConstruction) {
    YieldCurve curve = YieldCurve::ofZeroRates(
            {1, 2, 5, 10}, {0.030, 0.030, 0.030, 0.030});  // flat: f(0,t) = 3%
    // At t = 0 with r = f(0,0), P(0,T) must come back exactly.
    double f0 = short_rate_models::instantaneousForward(curve, 0);
    EXPECT_NEAR(f0, 0.03, 1e-6);  // flat curve: instantaneous forward = the rate
    EXPECT_NEAR(short_rate_models::hullWhiteBond(curve, 0, 5, f0, 0.1, 0.01),
                curve.discountFactor(5), 1e-6);
    // Away from the curve rate, higher r -> cheaper bond.
    EXPECT_TRUE(short_rate_models::hullWhiteBond(curve, 1, 5, 0.05, 0.1, 0.01)
                < short_rate_models::hullWhiteBond(curve, 1, 5, 0.02, 0.1, 0.01));

    // A steep short end: z(t) = 0.02 + 0.01t exactly (two pillars, linear
    // interpolation), so f(0,t) = 0.02 + 0.02t and g = -ln P is quadratic
    // — the short-end stencil is exact there, where a clamped central
    // difference would report f near (t+h)/2 instead.
    YieldCurve steep = YieldCurve::ofZeroRates({1e-4, 2}, {0.020001, 0.04});
    EXPECT_NEAR(short_rate_models::instantaneousForward(steep, 0.001), 0.02002, 1e-7);
    // Negative valuation time is rejected, not silently clamped to 0.
    EXPECT_THROWS(short_rate_models::instantaneousForward(steep, -5));
}

QTEST_MAIN()
