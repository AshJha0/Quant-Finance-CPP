// Port of credit.CreditTest, credit.CreditRoundTripTest and
// credit.CvaTest (Java): the bootstrap reprices every input exactly, the
// credit triangle spread ~ h*(1-R) holds on a flat curve, upfront is
// linear (exactly antisymmetric) in the coupon, hazards extrapolate
// flat, Z-spreads round-trip, and CVA matches hand sums and is linear in
// exposure and LGD. Pins and tolerances unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/credit/cds_pricer.hpp"
#include "quantfinlib/credit/credit_curve.hpp"
#include "quantfinlib/credit/credit_spreads.hpp"
#include "quantfinlib/credit/cva_approximator.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

using quantfinlib::CreditCurve;
using quantfinlib::YieldCurve;
namespace cds_pricer = quantfinlib::cds_pricer;
namespace credit_spreads = quantfinlib::credit_spreads;
namespace cva_approximator = quantfinlib::cva_approximator;

namespace {

YieldCurve flat3() {
    return YieldCurve::ofZeroRates({1, 2, 3, 5, 7, 10},
                                   {0.03, 0.03, 0.03, 0.03, 0.03, 0.03});
}

std::vector<double> quarterlyGrid(double years) {
    int n = static_cast<int>(std::lround(years * 4));
    std::vector<double> t(n);
    for (int i = 0; i < n; i++) {
        t[i] = (i + 1) * 0.25;
    }
    return t;
}

}  // namespace

// ------------------------------------------------------------ CreditTest

TEST(flatSpreadsSatisfyTheCreditTriangle) {
    // 100bp flat, R = 40%: h ~ S/(1-R) = 1.667%. The quarterly
    // discretization and accrual-on-default term move it by well under a
    // basis point of hazard.
    CreditCurve curve = CreditCurve::bootstrap({1, 3, 5}, {0.01, 0.01, 0.01},
                                               0.40, flat3());
    EXPECT_NEAR(curve.hazard(2.0), 0.01 / 0.60, 3e-4);
    EXPECT_NEAR(curve.hazard(4.0), 0.01 / 0.60, 3e-4);
    // Survival is monotone from 1 and consistent with its complement.
    EXPECT_NEAR(curve.survivalProbability(0), 1.0, 0.0);
    EXPECT_TRUE(curve.survivalProbability(1) > curve.survivalProbability(5));
    EXPECT_NEAR(curve.defaultProbability(3), 1 - curve.survivalProbability(3), 0.0);
}

TEST(bootstrapRepricesEveryPillarExactly) {
    std::vector<int> tenors = {1, 3, 5, 7};
    std::vector<double> spreads = {0.008, 0.012, 0.015, 0.016};  // upward credit curve
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap(tenors, spreads, 0.40, discount);
    for (std::size_t i = 0; i < tenors.size(); i++) {
        EXPECT_NEAR(cds_pricer::parSpread(curve, discount, tenors[i]),
                    spreads[i], 1e-10);
    }
    // Upward spreads need upward hazards.
    EXPECT_TRUE(curve.hazard(4.0) > curve.hazard(0.5));
}

TEST(upfrontIsZeroAtParAndPositiveForCheapCoupons) {
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({5}, {0.03}, 0.40, discount);
    double par = cds_pricer::parSpread(curve, discount, 5);
    EXPECT_NEAR(cds_pricer::upfront(curve, discount, par, 5), 0.0, 1e-12);
    // Standard 100bp contract coupon on a 300bp name: the buyer pays
    // points up front — roughly (300-100)bp times the risky annuity.
    double up = cds_pricer::upfront(curve, discount, 0.01, 5);
    EXPECT_TRUE(up > 0);
    EXPECT_NEAR(up, (par - 0.01) * cds_pricer::riskyAnnuity(curve, discount, 5), 1e-12);
}

TEST(creditGatesRefuseNonsense) {
    YieldCurve d = flat3();
    EXPECT_THROWS(CreditCurve::bootstrap({3, 1}, {0.01, 0.01}, 0.4, d));  // descending
    EXPECT_THROWS(CreditCurve::bootstrap({1}, {-0.01}, 0.4, d));          // negative spread
    EXPECT_THROWS(CreditCurve::bootstrap({1}, {0.01}, 1.0, d));           // recovery = 1
    EXPECT_THROWS(CreditCurve::bootstrap({1}, {100.0}, 0.4, d));          // no hazard fits
}

TEST(curvePricedBondHasZeroZSpreadAndRoundTrips) {
    YieldCurve curve = flat3();
    double onCurve = credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, 0);
    EXPECT_NEAR(credit_spreads::zSpread(onCurve, 100, 0.05, 2, 5, curve), 0.0, 1e-10);

    // A credit-risky price below the curve price implies positive z,
    // and the solved z reprices the bond exactly.
    double risky = onCurve * 0.95;
    double z = credit_spreads::zSpread(risky, 100, 0.05, 2, 5, curve);
    EXPECT_TRUE(z > 0);
    EXPECT_NEAR(credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, z),
                risky, 1e-9);
}

TEST(impossiblePricesThrowInsteadOfReturningTheBracketEdge) {
    YieldCurve curve = flat3();
    // Far above the maximum attainable PV (z = -50%).
    EXPECT_THROWS(credit_spreads::zSpread(1e7, 100, 0.05, 2, 5, curve));
    // Effectively free bond: below the z = 500% price.
    EXPECT_THROWS(credit_spreads::zSpread(1e-9, 100, 0.05, 2, 5, curve));
}

// -------------------------------------------------- CreditRoundTripTest

TEST(spreadsFromOneFlatHazardBootstrapBackToThatHazardEverywhere) {
    // Plant a single flat hazard by bootstrapping ONE pillar, read the
    // par spreads that hazard implies at 1/3/5/7/10y, then bootstrap a
    // fresh multi-pillar curve from those spreads: every recovered pillar
    // hazard must be the planted h, because a flat hazard is the unique
    // piecewise-constant solution repricing all of them.
    YieldCurve discount = flat3();
    CreditCurve base = CreditCurve::bootstrap({5}, {0.02}, 0.40, discount);
    double h = base.hazard(1.0);

    std::vector<int> tenors = {1, 3, 5, 7, 10};
    std::vector<double> spreads(tenors.size());
    for (std::size_t i = 0; i < tenors.size(); i++) {
        spreads[i] = cds_pricer::parSpread(base, discount, tenors[i]);
    }
    CreditCurve rebuilt = CreditCurve::bootstrap(tenors, spreads, 0.40, discount);
    for (double t : {0.5, 2.0, 4.0, 6.0, 9.0}) {
        EXPECT_NEAR(rebuilt.hazard(t), h, 1e-9);
    }
    // And the survival curves coincide, not just the local hazards.
    EXPECT_NEAR(rebuilt.survivalProbability(8), base.survivalProbability(8), 1e-9);
}

TEST(bootstrapRepricesEveryPillarOnASlopedDiscountCurveToo) {
    // Every other credit pin discounts at a FLAT curve, which a
    // DF-handling bug could hide behind; an upward-sloping curve makes
    // each period's discount factor distinct, so repricing all pillars
    // to 1e-12 exercises the discounting for real.
    YieldCurve sloped = YieldCurve::ofZeroRates(
            {1, 2, 3, 5, 7, 10}, {0.020, 0.024, 0.027, 0.030, 0.032, 0.033});
    std::vector<int> tenors = {1, 3, 5, 7, 10};
    std::vector<double> spreads = {0.008, 0.011, 0.014, 0.015, 0.016};
    CreditCurve curve = CreditCurve::bootstrap(tenors, spreads, 0.40, sloped);
    for (std::size_t i = 0; i < tenors.size(); i++) {
        EXPECT_NEAR(cds_pricer::parSpread(curve, sloped, tenors[i]),
                    spreads[i], 1e-12);
        // Par coupon means zero upfront.
        EXPECT_NEAR(cds_pricer::upfront(curve, sloped, spreads[i], tenors[i]),
                    0.0, 1e-14);
    }
}

TEST(creditTriangleErrorShrinksWithTheSpreadLevel) {
    // The triangle S = h*(1-R) is the flat-curve zeroth-order identity;
    // the discretization/accrual correction is O(spread), so the
    // RELATIVE error at 10bp must be far below the error at 500bp.
    YieldCurve discount = flat3();
    CreditCurve tight = CreditCurve::bootstrap({5}, {0.001}, 0.40, discount);
    CreditCurve wide = CreditCurve::bootstrap({5}, {0.05}, 0.40, discount);
    double errTight = std::fabs(tight.hazard(1) - 0.001 / 0.60) / (0.001 / 0.60);
    double errWide = std::fabs(wide.hazard(1) - 0.05 / 0.60) / (0.05 / 0.60);
    EXPECT_TRUE(errTight < 1e-3);
    EXPECT_TRUE(errWide > errTight);
}

TEST(upfrontIsExactlyAntisymmetricAroundThePar) {
    // upfront(S_c) = protection - S_c * annuity is LINEAR in the coupon,
    // so equal coupon distances above and below par produce upfronts of
    // equal size and opposite sign: a rich coupon means the protection
    // BUYER receives points (negative upfront).
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({5}, {0.03}, 0.40, discount);
    double par = cds_pricer::parSpread(curve, discount, 5);
    double annuity = cds_pricer::riskyAnnuity(curve, discount, 5);
    double d = 0.01;
    double upCheap = cds_pricer::upfront(curve, discount, par - d, 5);
    double upRich = cds_pricer::upfront(curve, discount, par + d, 5);
    EXPECT_NEAR(upCheap, d * annuity, 1e-12);
    EXPECT_NEAR(upRich, -d * annuity, 1e-12);
    EXPECT_NEAR(upRich, -upCheap, 1e-12);  // linear in coupon: exact antisymmetry
    EXPECT_TRUE(upRich < 0);  // coupon above par: seller pays the buyer points
    // Premium leg is spread times the annuity by definition.
    EXPECT_NEAR(cds_pricer::premiumLegPv(curve, discount, 0.02, 5),
                0.02 * annuity, 1e-15);
}

TEST(survivalIsStrictlyMonotoneAndExtrapolatesTheLastHazardFlat) {
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({1, 3, 5}, {0.01, 0.015, 0.02},
                                               0.40, discount);
    // Strictly decreasing survival on a fine grid.
    double prev = 1.0;
    for (double t = 0.5; t <= 12; t += 0.5) {
        double q = curve.survivalProbability(t);
        EXPECT_TRUE(q < prev);
        EXPECT_TRUE(q > 0);
        prev = q;
    }
    // Beyond the last pillar the hazard is flat, so
    // Q(5+s) = Q(5) * exp(-h_last * s) EXACTLY.
    double hLast = curve.hazard(6.0);
    EXPECT_NEAR(curve.hazard(5.0), hLast, 0.0);  // flat beyond the last pillar
    EXPECT_NEAR(curve.survivalProbability(8),
                curve.survivalProbability(5) * std::exp(-hLast * 3), 1e-15);
}

TEST(gatesTheFirstLayerMissed) {
    YieldCurve d = flat3();
    EXPECT_THROWS(CreditCurve::bootstrap({1}, {0.01}, -0.1, d));   // negative recovery
    EXPECT_THROWS(CreditCurve::bootstrap({0, 1}, {0.01, 0.01}, 0.4, d));  // zero tenor
    EXPECT_THROWS(CreditCurve::bootstrap({1}, {std::nan("")}, 0.4, d));   // NaN spread
    CreditCurve curve = CreditCurve::bootstrap({1}, {0.01}, 0.4, d);
    EXPECT_THROWS(curve.survivalProbability(-1));
    EXPECT_THROWS(curve.hazard(-0.5));
    EXPECT_THROWS(cds_pricer::parSpread(curve, d, 0));           // zero maturity
    EXPECT_THROWS(cds_pricer::upfront(curve, d, -0.01, 5));      // negative coupon
    // Below one quarterly grid step the leg sums are EMPTY: par spread
    // would be 0/0 = NaN. The gate throws instead of leaking it.
    EXPECT_THROWS(cds_pricer::parSpread(curve, d, 0.1));         // sub-grid maturity
    EXPECT_THROWS(cds_pricer::riskyAnnuity(curve, d, 0.2));      // sub-grid maturity
}

TEST(plantedZSpreadsRoundTripAndPriceFallsInZ) {
    YieldCurve curve = flat3();
    // Plant z, price, solve back: the solver must return the plant.
    for (double z : {0.005, 0.02, 0.08}) {
        double px = credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, z);
        EXPECT_NEAR(credit_spreads::zSpread(px, 100, 0.05, 2, 5, curve), z, 1e-10);
    }
    // More z discounts every flow harder: price strictly decreasing.
    double p0 = credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, 0.00);
    double p1 = credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, 0.02);
    double p2 = credit_spreads::priceWithZSpread(100, 0.05, 2, 5, curve, 0.08);
    EXPECT_TRUE(p0 > p1 && p1 > p2);
}

TEST(cvaIsLinearInBothExposureAndLgd) {
    // CVA = LGD * sum EE_i * dPD_i * DF_i: doubling the exposure profile
    // or the LGD doubles the number exactly.
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({5}, {0.02}, 0.40, discount);
    std::vector<double> t = {0.5, 1.0, 1.5, 2.0};
    std::vector<double> ee = {100, 250, 175, 50};
    std::vector<double> ee2 = {200, 500, 350, 100};
    double base = cva_approximator::cva(ee, t, curve, discount, 0.3);
    EXPECT_TRUE(base > 0);
    EXPECT_NEAR(cva_approximator::cva(ee2, t, curve, discount, 0.3), 2 * base, 1e-12);
    EXPECT_NEAR(cva_approximator::cva(ee, t, curve, discount, 0.6), 2 * base, 1e-12);
}

TEST(twoPeriodCvaMatchesTheHandSum) {
    // Two buckets, hand-computed: with flat hazard h and flat 3% cc
    // discounting,
    //   CVA = LGD * [ EE1 * (1 - e^{-h/2}) * e^{-0.015}
    //               + EE2 * (e^{-h/2} - e^{-h}) * e^{-0.03} ].
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({1}, {0.02}, 0.40, discount);
    double h = curve.hazard(0.5);
    double lgd = 0.6;
    double expected = lgd * (100 * (1 - std::exp(-h * 0.5)) * std::exp(-0.03 * 0.5)
            + 200 * (std::exp(-h * 0.5) - std::exp(-h)) * std::exp(-0.03 * 1.0));
    EXPECT_NEAR(cva_approximator::cva({100, 200}, {0.5, 1.0}, curve, discount, lgd),
                expected, 1e-12);
}

// --------------------------------------------------------------- CvaTest

TEST(flatExposureFlatHazardMatchesTheHandSum) {
    YieldCurve discount = flat3();
    // Single 10y pillar: the bootstrapped hazard is flat, so
    // Q(t) = e^{-h t} exactly and the CVA sum is pure arithmetic.
    CreditCurve curve = CreditCurve::bootstrap({10}, {0.02}, 0.40, discount);
    double h = curve.hazard(1.0);
    double lgd = 0.6;
    std::vector<double> grid = quarterlyGrid(5);
    std::vector<double> ee(grid.size(), 1000000.0);

    double expected = 0;
    double prevT = 0;
    for (double t : grid) {
        expected += 1000000 * (std::exp(-h * prevT) - std::exp(-h * t))
                * std::exp(-0.03 * t);
        prevT = t;
    }
    expected *= lgd;
    EXPECT_NEAR(cva_approximator::cva(ee, grid, curve, discount, lgd), expected, 1e-6);
    // Order of magnitude sanity: LGD * EE * 5y default probability,
    // lightly discounted — tens of thousands on a million of EE.
    EXPECT_TRUE(expected > 50000 && expected < 120000);
}

TEST(risklessCounterpartyCostsNothing) {
    YieldCurve discount = flat3();
    // The bootstrap gates demand a positive spread; a vanishing one is
    // the riskless limit and the CVA must vanish with it.
    CreditCurve curve = CreditCurve::bootstrap({5}, {1e-8}, 0.40, discount);
    std::vector<double> grid = quarterlyGrid(5);
    std::vector<double> ee(grid.size(), 1.0);
    EXPECT_NEAR(cva_approximator::cva(ee, grid, curve, discount, 0.6), 0, 1e-6);
}

TEST(cvaIncreasesWithTheSpreadLevel) {
    YieldCurve discount = flat3();
    CreditCurve tight = CreditCurve::bootstrap({1, 3, 5}, {0.01, 0.01, 0.01},
                                               0.40, discount);
    CreditCurve wide = CreditCurve::bootstrap({1, 3, 5}, {0.02, 0.02, 0.02},
                                              0.40, discount);
    std::vector<double> grid = quarterlyGrid(5);
    std::vector<double> ee(grid.size(), 1000000.0);
    double cvaTight = cva_approximator::cva(ee, grid, tight, discount, 0.6);
    double cvaWide = cva_approximator::cva(ee, grid, wide, discount, 0.6);
    EXPECT_TRUE(cvaTight > 0);
    EXPECT_TRUE(cvaWide > cvaTight);
    // Not quite 2x (survival decays faster on the wide curve), but close.
    EXPECT_TRUE(cvaWide < 2 * cvaTight);
}

TEST(cvaGatesRefuseNonsense) {
    YieldCurve discount = flat3();
    CreditCurve curve = CreditCurve::bootstrap({5}, {0.01}, 0.40, discount);
    std::vector<double> okT = {0.5, 1.0};
    std::vector<double> okE = {1.0, 1.0};
    EXPECT_THROWS(cva_approximator::cva({}, {}, curve, discount, 0.6));  // empty
    EXPECT_THROWS(cva_approximator::cva(okE, {1.0}, curve, discount, 0.6));  // misaligned
    EXPECT_THROWS(cva_approximator::cva(okE, {1.0, 0.5}, curve, discount, 0.6));  // non-ascending
    EXPECT_THROWS(cva_approximator::cva(okE, {0, 1.0}, curve, discount, 0.6));    // t[0] = 0
    EXPECT_THROWS(cva_approximator::cva(okE, {0.5, std::nan("")}, curve, discount,
                                        0.6));                                    // NaN time
    EXPECT_THROWS(cva_approximator::cva({-1, 1}, okT, curve, discount, 0.6));  // negative EE
    EXPECT_THROWS(cva_approximator::cva({std::nan(""), 1}, okT, curve, discount,
                                        0.6));                                 // NaN EE
    EXPECT_THROWS(cva_approximator::cva(okE, okT, curve, discount, 0));    // lgd = 0
    EXPECT_THROWS(cva_approximator::cva(okE, okT, curve, discount, 1.2));  // lgd > 1
}

QTEST_MAIN()
