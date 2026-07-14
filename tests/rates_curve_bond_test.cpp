// Port of rates.RatesTest plus the YTM bracket gate from
// ReviewFixesRoundTest and the key-rate-duration pin from
// pricing.MarketRiskPricingTest (Java). All pins and tolerances carried
// over unchanged; derivations copied from the Java comments.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/rates/bond_pricer.hpp"
#include "quantfinlib/rates/key_rate_durations.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

using quantfinlib::YieldCurve;
namespace bond_pricer = quantfinlib::bond_pricer;
namespace key_rate_durations = quantfinlib::key_rate_durations;

// ------------------------------------------------------------ YieldCurve

TEST(bootstrapFlatParCurveRecoversFlatZeros) {
    // Flat 5% annual par swaps -> DF_n = 1.05^-n, zero_cc = ln(1.05).
    YieldCurve curve = YieldCurve::bootstrapAnnualParSwaps(
            {1, 2, 3, 5}, {0.05, 0.05, 0.05, 0.05});
    for (int y = 1; y <= 5; y++) {
        EXPECT_NEAR(curve.discountFactor(y), std::pow(1.05, -y), 1e-10);
        EXPECT_NEAR(curve.zeroRate(y), std::log(1.05), 1e-10);
    }
}

TEST(upwardSlopingParCurveImpliesHigherForwards) {
    YieldCurve curve = YieldCurve::bootstrapAnnualParSwaps(
            {1, 2, 3}, {0.02, 0.03, 0.04});
    EXPECT_TRUE(curve.zeroRate(3) > curve.zeroRate(1));
    // Forward 2y->3y must exceed the 3y zero on an upward-sloping curve.
    EXPECT_TRUE(curve.forwardRate(2, 3) > curve.zeroRate(3));
    EXPECT_TRUE(curve.discountFactor(3) < curve.discountFactor(2));
}

TEST(zeroRateInterpolatesAndExtrapolatesFlat) {
    YieldCurve curve = YieldCurve::ofZeroRates({1, 3}, {0.02, 0.04});
    EXPECT_NEAR(curve.zeroRate(2), 0.03, 1e-12);
    EXPECT_NEAR(curve.zeroRate(0.5), 0.02, 1e-12);   // flat short end
    EXPECT_NEAR(curve.zeroRate(10), 0.04, 1e-12);    // flat long end
    EXPECT_NEAR(curve.discountFactor(0), 1.0, 1e-12);
}

TEST(yieldCurveGatesRefuseNonsense) {
    EXPECT_THROWS(YieldCurve::ofZeroRates({}, {}));                 // empty
    EXPECT_THROWS(YieldCurve::ofZeroRates({1, 2}, {0.02}));         // misaligned
    EXPECT_THROWS(YieldCurve::ofZeroRates({0}, {0.02}));            // zero tenor
    EXPECT_THROWS(YieldCurve::bootstrapAnnualParSwaps({}, {}));     // empty
    YieldCurve c = YieldCurve::ofZeroRates({1, 3}, {0.02, 0.04});
    EXPECT_THROWS(c.forwardRate(3, 3));                             // to <= from
    EXPECT_THROWS(c.forwardRate(3, 2));
}

// ------------------------------------------------------------ BondPricer

TEST(zeroCouponBondAnalytics) {
    // 5y zero-coupon at 4% annual yield.
    double price = bond_pricer::priceFromYield(100, 0, 1, 5, 0.04);
    EXPECT_NEAR(price, 100 / std::pow(1.04, 5), 1e-10);
    // Zero's Macaulay duration equals its maturity.
    EXPECT_NEAR(bond_pricer::macaulayDuration(100, 0, 1, 5, 0.04), 5.0, 1e-10);
    EXPECT_NEAR(bond_pricer::modifiedDuration(100, 0, 1, 5, 0.04), 5.0 / 1.04, 1e-10);
}

TEST(parBondPricesAtFaceAndYtmRoundTrips) {
    // Coupon == yield -> price == face.
    EXPECT_NEAR(bond_pricer::priceFromYield(100, 0.06, 2, 10, 0.06), 100, 1e-9);
    double price = bond_pricer::priceFromYield(100, 0.05, 2, 7, 0.043);
    EXPECT_NEAR(bond_pricer::yieldToMaturity(price, 100, 0.05, 2, 7), 0.043, 1e-8);
}

TEST(convexityAndDv01ArePositiveAndConsistent) {
    double convexity = bond_pricer::convexity(100, 0.05, 2, 10, 0.05);
    EXPECT_TRUE(convexity > 0);
    double dv01 = bond_pricer::dv01(100, 0.05, 2, 10, 0.05);
    // First-order check: price change for 1bp ~ DV01.
    double p0 = bond_pricer::priceFromYield(100, 0.05, 2, 10, 0.05);
    double p1 = bond_pricer::priceFromYield(100, 0.05, 2, 10, 0.0501);
    EXPECT_NEAR(p0 - p1, dv01, dv01 * 0.01);
}

TEST(curvePricingMatchesYieldPricingOnFlatCurve) {
    // Flat cc curve at z; equivalent semi-annual yield y = 2(e^{z/2}-1).
    double z = 0.05;
    YieldCurve flat = YieldCurve::ofZeroRates({1, 30}, {z, z});
    double y = 2 * (std::exp(z / 2) - 1);
    double fromCurve = bond_pricer::priceFromCurve(100, 0.06, 2, 10, flat);
    double fromYield = bond_pricer::priceFromYield(100, 0.06, 2, 10, y);
    EXPECT_NEAR(fromCurve, fromYield, 1e-6);
}

// Port of ReviewFixesRoundTest.impossibleBondPriceThrowsInsteadOfReturningTheEndpoint.
TEST(impossibleBondPriceThrowsInsteadOfReturningTheEndpoint) {
    // A 2y zero at "price 50,000 per 100 face" has no yield above -90%.
    EXPECT_THROWS(bond_pricer::yieldToMaturity(50000, 100, 0.0, 1, 2));
    EXPECT_THROWS(bond_pricer::yieldToMaturity(0.0001, 100, 0.0, 1, 2));
    // Sane prices still solve: par bond yields its coupon.
    EXPECT_NEAR(bond_pricer::yieldToMaturity(100, 100, 0.05, 2, 5), 0.05, 1e-9);
}

// ---------------------------------------------------- key-rate durations
// Port of MarketRiskPricingTest.keyRateSlicesAddBackUpToTheParallelDv01.

TEST(keyRateSlicesAddBackUpToTheParallelDv01) {
    YieldCurve curve = YieldCurve::ofZeroRates(
            {1, 2, 5, 10}, {0.02, 0.025, 0.03, 0.032});
    std::vector<double> krd = key_rate_durations::keyRateDv01s(100, 0.04, 2, 5, curve);
    double parallel = key_rate_durations::parallelDv01(100, 0.04, 2, 5, curve);

    double sum = 0;
    std::size_t maxAt = 0;
    for (std::size_t i = 0; i < krd.size(); i++) {
        sum += krd[i];
        if (krd[i] > krd[maxAt]) {
            maxAt = i;
        }
    }
    // The slices reassemble the parallel move (interpolation tolerance).
    EXPECT_NEAR(sum, parallel, 0.02 * parallel);
    EXPECT_TRUE(maxAt == 2);            // a 5y bond's rate risk lives at the 5y node
    EXPECT_TRUE(parallel > 0);          // rates up, bond down — DV01 sign convention
    EXPECT_TRUE(krd[3] < krd[2]);       // little risk beyond the bond's maturity
}

QTEST_MAIN()
