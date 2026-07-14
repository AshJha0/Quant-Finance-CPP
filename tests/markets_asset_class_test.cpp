// Port of markets.AssetClassRoundTest (commodities, index construction,
// private markets sections) and markets.AssetClassEdgeTest (Java). The
// swap section of AssetClassRoundTest lives in
// rates_models_options_test.cpp. Pins and tolerances unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/commodities/commodity_curve.hpp"
#include "quantfinlib/markets/index_construction.hpp"
#include "quantfinlib/markets/private_market_analytics.hpp"

using quantfinlib::CommodityCurve;
namespace index_construction = quantfinlib::index_construction;
namespace pm = quantfinlib::private_market_analytics;

namespace {

void expectVectorNear(const std::vector<double>& actual,
                      const std::vector<double>& expected, double tol) {
    EXPECT_TRUE(actual.size() == expected.size());
    for (std::size_t i = 0; i < actual.size() && i < expected.size(); i++) {
        EXPECT_NEAR(actual[i], expected[i], tol);
    }
}

}  // namespace

// ----------------------------------------------------------- commodities

TEST(contangoCurveChargesTheLongAndCarryIsExact) {
    CommodityCurve curve = CommodityCurve::of(100, {0.25, 0.5, 1.0}, {101, 102, 104});
    EXPECT_TRUE(curve.isContango());
    EXPECT_TRUE(!curve.isBackwardation());
    EXPECT_NEAR(curve.price(0.75), 103, 1e-12);   // linear between pillars
    EXPECT_TRUE(curve.annualizedRollYield(0.25, 1.0) < 0);  // rolling long in contango pays away
    // F = S e^{(r+u-y)t}: implied u-y = ln(104/100)/1 - 3%.
    EXPECT_NEAR(curve.impliedCarry(1.0, 0.03), std::log(1.04) - 0.03, 1e-12);
}

TEST(backwardationPaysTheRollAndGatesHold) {
    CommodityCurve curve = CommodityCurve::of(100, {0.25, 0.5, 1.0}, {99, 97, 95});
    EXPECT_TRUE(curve.isBackwardation());
    EXPECT_NEAR(curve.annualizedRollYield(0.25, 1.0),
                std::log(99.0 / 95.0) / 0.75, 1e-12);
    EXPECT_THROWS(curve.price(2.0));                                // no extrapolation
    EXPECT_THROWS(CommodityCurve::of(100, {0.5, 0.25}, {99, 98}));  // descending
    EXPECT_THROWS(curve.annualizedRollYield(1.0, 0.5));             // far <= near
}

TEST(flatCommodityCurveIsNeitherShapeAndCarriesMinusR) {
    // All futures AT spot: both shape tests are strict, so both are
    // false; ln(F/F) = 0 makes the roll yield exactly zero and the
    // implied carry exactly -r (ln(F/S)/t = 0).
    CommodityCurve flat = CommodityCurve::of(100, {0.25, 0.5, 1.0}, {100, 100, 100});
    EXPECT_TRUE(!flat.isContango());
    EXPECT_TRUE(!flat.isBackwardation());
    EXPECT_NEAR(flat.annualizedRollYield(0.25, 1.0), 0.0, 0.0);
    EXPECT_NEAR(flat.impliedCarry(1.0, 0.03), -0.03, 0.0);
    EXPECT_NEAR(flat.spot(), 100, 0.0);
}

TEST(pillarPricesAreExactAndBothWingsRefuseToExtrapolate) {
    CommodityCurve curve = CommodityCurve::of(100, {0.5, 1.0}, {102, 104});
    EXPECT_NEAR(curve.price(0.5), 102, 0.0);    // exact at the pillar
    EXPECT_NEAR(curve.price(0.75), 103, 1e-12); // linear midpoint
    EXPECT_THROWS(curve.price(0.25));           // before first
    EXPECT_THROWS(curve.price(1.5));            // after last
    EXPECT_THROWS(curve.impliedCarry(1.0, std::nan("")));
    EXPECT_THROWS(CommodityCurve::of(0, {1}, {100}));   // zero spot
    EXPECT_THROWS(CommodityCurve::of(100, {1}, {-5}));  // negative price
}

// ---------------------------------------------------- index construction

TEST(weightSchemesMatchHandArithmetic) {
    expectVectorNear(index_construction::capWeights({10, 20}, {100, 50}, {1, 1}),
                     {0.5, 0.5}, 1e-12);
    expectVectorNear(index_construction::capWeights({10, 20}, {100, 50}, {1, 0.5}),
                     {2.0 / 3, 1.0 / 3}, 1e-12);
    // The Dow accident: the expensive stock owns the index.
    expectVectorNear(index_construction::priceWeights({50, 400}),
                     {1.0 / 9, 8.0 / 9}, 1e-12);
    expectVectorNear(index_construction::equalWeights(4),
                     {0.25, 0.25, 0.25, 0.25}, 0.0);
}

TEST(divisorAdjustmentKeepsTheLevelContinuousThroughAMemberSwap) {
    std::vector<double> p = {10, 20};
    std::vector<double> s = {100, 50};
    std::vector<double> f = {1, 1};
    double divisor = 20;
    double level = index_construction::level(p, s, f, divisor);  // 2000/20 = 100
    EXPECT_NEAR(level, 100, 1e-12);

    // Swap member 2 (cap 1000) for a new stock with cap 1200: the
    // divisor rescales 20 * 2200/2000 = 22 and the level holds at 100.
    double newDivisor = index_construction::adjustDivisor(divisor, 2000, 2200);
    EXPECT_NEAR(newDivisor, 22, 1e-12);
    // Membership changes must not move the index.
    EXPECT_NEAR(index_construction::level({10, 30}, {100, 40}, f, newDivisor),
                level, 1e-12);
}

TEST(turnoverIsHalfTheAbsoluteWeightShift) {
    EXPECT_NEAR(index_construction::turnover({0.5, 0.5}, {0.6, 0.4}), 0.1, 1e-12);
    EXPECT_NEAR(index_construction::turnover({0.3, 0.7}, {0.3, 0.7}), 0.0, 0.0);
}

TEST(weightsNormalizeToOneAndTurnoverMatchesASecondHandPin) {
    // priceWeights {20,30,50} -> {0.2, 0.3, 0.5} exactly.
    expectVectorNear(index_construction::priceWeights({20, 30, 50}),
                     {0.2, 0.3, 0.5}, 1e-15);
    std::vector<double> cap = index_construction::capWeights(
            {11, 23, 47}, {9, 13, 3}, {1, 0.7, 0.4});
    double sum = cap[0] + cap[1] + cap[2];
    EXPECT_NEAR(sum, 1.0, 1e-15);  // cap weights are a probability vector
    // Equal quarter weights to {0.4, 0.3, 0.2, 0.1}:
    // 0.5 * (0.15 + 0.05 + 0.05 + 0.15) = 0.2.
    EXPECT_NEAR(index_construction::turnover(index_construction::equalWeights(4),
                                             {0.4, 0.3, 0.2, 0.1}),
                0.2, 1e-12);
}

TEST(indexGatesRefuseNonsense) {
    EXPECT_THROWS(index_construction::turnover({0.5, 0.5}, {1.0}));
    EXPECT_THROWS(index_construction::capWeights({10}, {100}, {1.5}));  // float > 1
    EXPECT_THROWS(index_construction::level({10}, {100}, {1}, 0));      // divisor 0
    EXPECT_THROWS(index_construction::equalWeights(0));
    EXPECT_THROWS(index_construction::adjustDivisor(-1, 100, 110));
    EXPECT_THROWS(index_construction::priceWeights({10, -5}));
}

// ------------------------------------------------------- private markets

TEST(irrRecoversPlantedRatesAndRefusesSignlessFlows) {
    EXPECT_NEAR(pm::irr({-100, 110}), 0.10, 1e-9);
    // -100 + 10/1.1 + 110/1.21 = 0 exactly: IRR 10% with an interim flow.
    EXPECT_NEAR(pm::irr({-100, 10, 110}), 0.10, 1e-9);
    EXPECT_THROWS(pm::irr({-100, -10, -5}));
    EXPECT_THROWS(pm::irr({100}));
}

TEST(multiplesAreExactFractions) {
    EXPECT_NEAR(pm::tvpi(100, 80, 40), 1.2, 1e-12);
    EXPECT_NEAR(pm::dpi(100, 80, 40), 0.8, 1e-12);
    EXPECT_NEAR(pm::rvpi(100, 80, 40), 0.4, 1e-12);
    EXPECT_THROWS(pm::tvpi(0, 80, 40));
}

TEST(ksPmeIsExactlyOneWhenTheFundIsTheIndex) {
    // Invest 100 at t0, let it ride the index to 121: NAV 121,
    // FV(contribution) = 100 * 121/100 = 121 -> PME = 1 exactly.
    double pme = pm::ksPme({100, 0, 0}, {0, 0, 0}, 121, {100, 110, 121});
    EXPECT_NEAR(pme, 1.0, 1e-12);
    // Beating the index: same flows, higher NAV.
    EXPECT_TRUE(pm::ksPme({100, 0, 0}, {0, 0, 0}, 150, {100, 110, 121}) > 1);
}

TEST(geltnerDesmoothingInvertsSmoothingExactly) {
    std::vector<double> truth = {0.02, -0.01, 0.03, 0.015, -0.02, 0.01};
    double phi = 0.4;
    std::vector<double> observed(truth.size());
    observed[0] = truth[0];
    for (std::size_t t = 1; t < truth.size(); t++) {
        observed[t] = (1 - phi) * truth[t] + phi * observed[t - 1];
    }
    std::vector<double> recovered = pm::geltnerDesmooth(observed, phi);
    // Smoothing then desmoothing must round-trip.
    expectVectorNear(recovered, truth, 1e-12);
    EXPECT_THROWS(pm::geltnerDesmooth(observed, 1.0));
}

TEST(irrHitsPlantedDoublingAndHalvingRates) {
    // Invest 100 today, receive 200 in one period:
    // -100 + 200/(1+r) = 0 -> r = 1.0 (a 100% money-weighted return).
    EXPECT_NEAR(pm::irr({-100, 200}), 1.0, 1e-9);
    // Receive only 50: 1+r = 0.5 -> r = -0.5.
    EXPECT_NEAR(pm::irr({-100, 50}), -0.5, 1e-9);
    // Break-even: r = 0 exactly.
    EXPECT_NEAR(pm::irr({-100, 100}), 0.0, 1e-9);
}

TEST(tvpiIsDpiPlusRvpiOnArbitraryNumbers) {
    // (D + NAV)/C = D/C + NAV/C: the accounting identity must hold on
    // numbers with no nice factors.
    double c = 137, d = 61, nav = 45;
    EXPECT_NEAR(pm::tvpi(c, d, nav), pm::dpi(c, d, nav) + pm::rvpi(c, d, nav), 1e-15);
}

TEST(ksPmeWithDistributionsMatchesTheHandFractionAndFlagsLaggards) {
    // Flat index: every growth factor is 1, so
    // PME = (30 + 100) / (50 + 50) = 1.3 exactly.
    EXPECT_NEAR(pm::ksPme({50, 50, 0}, {0, 0, 30}, 100, {100, 100, 100}),
                1.3, 1e-15);
    // Index up 10% after the contribution, fund NAV only 80:
    // PME = 80 / (100 * 110/100) = 8/11 < 1 — the fund lagged.
    EXPECT_NEAR(pm::ksPme({100, 0}, {0, 0}, 80, {100, 110}),
                80.0 / 110.0, 1e-15);
    EXPECT_THROWS(pm::ksPme({0, 0}, {0, 0}, 100, {100, 110}));
}

TEST(geltnerWithZeroPhiIsTheIdentityAndNegativePhiThrows) {
    // phi = 0: r_true = (r_obs - 0) / 1 = r_obs, element for element.
    std::vector<double> obs = {0.02, -0.01, 0.03, 0.005};
    expectVectorNear(pm::geltnerDesmooth(obs, 0.0), obs, 0.0);
    EXPECT_THROWS(pm::geltnerDesmooth(obs, -0.1));
    EXPECT_THROWS(pm::geltnerDesmooth({0.02}, 0.4));
}

QTEST_MAIN()
