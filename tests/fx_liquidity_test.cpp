// Port of fx.FixingRiskCrossRateTest (FixingRisk half only -- CrossRateEngine
// is skipped, see synthetic_cross.hpp), fx.SyntheticCrossTest, fx.FxTierBookTest
// and fx.LpScorecardAndRouterTest (Java): fixing-window analytics, direct-vs-
// synthetic cross execution arithmetic, the tiered multi-LP book, and the
// last-look-aware scorecard/router pair. Pins and tolerances unchanged.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/fx/fixing_risk.hpp"
#include "quantfinlib/fx/fx_tier_book.hpp"
#include "quantfinlib/fx/lp_router.hpp"
#include "quantfinlib/fx/lp_scorecard.hpp"
#include "quantfinlib/fx/synthetic_cross.hpp"

using quantfinlib::CrossOp;
using quantfinlib::CurrencyPair;
using quantfinlib::FxTierBook;
using quantfinlib::LpRouter;
using quantfinlib::LpScorecard;
namespace fixing_risk = quantfinlib::fixing_risk;
namespace synthetic_cross = quantfinlib::synthetic_cross;

// ------------------------------------------------------------ FixingRisk

TEST(windowAveragesAndSlippage) {
    std::vector<double> prices = {1.0850, 1.0852, 1.0854};
    std::vector<double> sizes = {1, 1, 2};
    EXPECT_NEAR(1.0852, fixing_risk::windowTwap(prices), 1e-12);
    EXPECT_NEAR((1.0850 + 1.0852 + 2 * 1.0854) / 4, fixing_risk::windowVwap(prices, sizes), 1e-12);
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    EXPECT_NEAR(2.0, fixing_risk::slippageVsFix(eurusd, 1.0852, 1.0850), 1e-9);
}

TEST(trackingErrorFollowsTheTwapVarianceLaw) {
    double oneX = fixing_risk::trackingErrorStd(0.0001, 5);
    double twoX = fixing_risk::trackingErrorStd(0.0001, 10);
    EXPECT_NEAR(std::sqrt(2.0), twoX / oneX, 1e-12);
    EXPECT_NEAR(0.0001 * std::sqrt(5.0 / 3), oneX, 1e-15);
    EXPECT_NEAR(0.25, fixing_risk::participationRate(25, 100), 1e-12);
    EXPECT_THROWS(fixing_risk::windowTwap({}));
    EXPECT_THROWS(fixing_risk::windowVwap({1}, {0}));
    EXPECT_THROWS(fixing_risk::trackingErrorStd(0.1, 0));
    EXPECT_THROWS(fixing_risk::participationRate(1, 0));
}

// --------------------------------------------------------- SyntheticCross

namespace {
constexpr double EU_BID = 1.0850;
constexpr double EU_ASK = 1.0852;
constexpr double UJ_BID = 161.50;
constexpr double UJ_ASK = 161.52;
}  // namespace

TEST(multiplyCrossesBothSpreads) {
    double synAsk = synthetic_cross::syntheticAsk(CrossOp::MULTIPLY, EU_BID, EU_ASK, UJ_BID, UJ_ASK);
    double synBid = synthetic_cross::syntheticBid(CrossOp::MULTIPLY, EU_BID, EU_ASK, UJ_BID, UJ_ASK);
    EXPECT_NEAR(EU_ASK * UJ_ASK, synAsk, 1e-9);
    EXPECT_NEAR(EU_BID * UJ_BID, synBid, 1e-9);
    EXPECT_TRUE(synAsk > synBid);
}

TEST(divideUsesOppositeSidesOfTheSharedQuoteLeg) {
    double gbBid = 1.2700;
    double gbAsk = 1.2702;
    double synAsk = synthetic_cross::syntheticAsk(CrossOp::DIVIDE, EU_BID, EU_ASK, gbBid, gbAsk);
    double synBid = synthetic_cross::syntheticBid(CrossOp::DIVIDE, EU_BID, EU_ASK, gbBid, gbAsk);
    EXPECT_NEAR(EU_ASK / gbBid, synAsk, 1e-9);
    EXPECT_NEAR(EU_BID / gbAsk, synBid, 1e-9);
    EXPECT_TRUE(synAsk > synBid);
}

TEST(routeChoiceFollowsTheCheaperAllIn) {
    double synAsk = EU_ASK * UJ_ASK;
    EXPECT_TRUE(synthetic_cross::buySyntheticWins(synAsk + 0.02, CrossOp::MULTIPLY,
                                                  EU_BID, EU_ASK, UJ_BID, UJ_ASK));
    EXPECT_TRUE(!synthetic_cross::buySyntheticWins(synAsk - 0.02, CrossOp::MULTIPLY,
                                                   EU_BID, EU_ASK, UJ_BID, UJ_ASK));
    double savings = synthetic_cross::buySavings(synAsk + 0.02, CrossOp::MULTIPLY,
                                                 EU_BID, EU_ASK, UJ_BID, UJ_ASK);
    EXPECT_NEAR(0.02, savings, 1e-9);
    double synBid = EU_BID * UJ_BID;
    EXPECT_TRUE(synthetic_cross::sellSyntheticWins(synBid - 0.02, CrossOp::MULTIPLY,
                                                   EU_BID, EU_ASK, UJ_BID, UJ_ASK));
    EXPECT_TRUE(!synthetic_cross::sellSyntheticWins(synBid + 0.02, CrossOp::MULTIPLY,
                                                    EU_BID, EU_ASK, UJ_BID, UJ_ASK));
}

TEST(unpricedRoutesNeverWin) {
    EXPECT_TRUE(!synthetic_cross::buySyntheticWins(std::nan(""), CrossOp::MULTIPLY,
                                                   EU_BID, EU_ASK, UJ_BID, UJ_ASK));
    EXPECT_TRUE(!synthetic_cross::buySyntheticWins(175.30, CrossOp::MULTIPLY,
                                                   EU_BID, std::nan(""), UJ_BID, UJ_ASK));
    EXPECT_TRUE(!synthetic_cross::sellSyntheticWins(175.20, CrossOp::MULTIPLY,
                                                    std::nan(""), EU_ASK, UJ_BID, UJ_ASK));
}

TEST(zeroPricedLegsNeverWinEither) {
    EXPECT_TRUE(!synthetic_cross::sellSyntheticWins(1.0850, CrossOp::DIVIDE, 1.10, 1.101, 1.27, 0.0));
    EXPECT_TRUE(!synthetic_cross::buySyntheticWins(175.30, CrossOp::MULTIPLY,
                                                   EU_BID, EU_ASK, UJ_BID, 0.0));
    EXPECT_TRUE(!synthetic_cross::buySyntheticWins(0.0, CrossOp::MULTIPLY, EU_BID, EU_ASK, UJ_BID, UJ_ASK));
    EXPECT_TRUE(std::isnan(synthetic_cross::buySavings(175.30, CrossOp::MULTIPLY,
                                                       EU_BID, -1.0, UJ_BID, UJ_ASK)));
}

// ----------------------------------------------------------- FxTierBook

namespace {

/** Two LPs with two-tier ladders around 1.0850, plus a silent LP2. */
FxTierBook tierBook() {
    FxTierBook b(3, 4);
    b.tier(0, false, 0, 1.08502, 1'000'000);
    b.tier(0, false, 1, 1.08504, 5'000'000);
    b.tierCount(0, false, 2);
    b.tier(0, true, 0, 1.08500, 1'000'000);
    b.tier(0, true, 1, 1.08498, 5'000'000);
    b.tierCount(0, true, 2);
    b.tier(1, false, 0, 1.08503, 2'000'000);
    b.tier(1, false, 1, 1.08505, 10'000'000);
    b.tierCount(1, false, 2);
    b.tier(1, true, 0, 1.08499, 2'000'000);
    b.tier(1, true, 1, 1.08497, 10'000'000);
    b.tierCount(1, true, 2);
    return b;
}

}  // namespace

TEST(compositeTopOfBookScansTierZero) {
    FxTierBook b = tierBook();
    EXPECT_NEAR(1.08500, b.bestBid(), 1e-12);
    EXPECT_NEAR(1.08502, b.bestAsk(), 1e-12);
}

TEST(sweepTakesCheapestTiersAcrossLps) {
    FxTierBook b = tierBook();
    double cost = b.sweepBuyCost(4'000'000);
    double expected = 1'000'000 * 1.08502 + 2'000'000 * 1.08503 + 1'000'000 * 1.08504;
    EXPECT_NEAR(expected, cost, 1e-4);
    double proceeds = b.sweepSellProceeds(4'000'000);
    double expSell = 1'000'000 * 1.08500 + 2'000'000 * 1.08499 + 1'000'000 * 1.08498;
    EXPECT_NEAR(expSell, proceeds, 1e-4);
}

TEST(sweepPlanAttributesQuantityPerLp) {
    FxTierBook b = tierBook();
    std::vector<double> plan(3, 0.0);
    double cost = b.sweepPlan(true, 4'000'000, plan);
    EXPECT_TRUE(cost > 0);
    EXPECT_NEAR(2'000'000, plan[0], 1e-9);
    EXPECT_NEAR(2'000'000, plan[1], 1e-9);
    EXPECT_NEAR(0, plan[2], 1e-9);
}

TEST(unfillableSizeIsNaNNotAPartialPrice) {
    FxTierBook b = tierBook();
    EXPECT_TRUE(std::isnan(b.sweepBuyCost(50'000'000)));
    EXPECT_TRUE(std::isnan(b.sweepSellProceeds(50'000'000)));
    EXPECT_TRUE(std::isnan(b.sweepBuyCost(0)));
}

TEST(fullAmountFindsTheSingleLpCoveringTheClip) {
    FxTierBook b = tierBook();
    EXPECT_NEAR(1.08505, b.bestFullAmountAsk(8'000'000), 1e-12);
    EXPECT_TRUE(b.bestFullAmountAskLp(8'000'000) == 1);
    EXPECT_NEAR(1.08497, b.bestFullAmountBid(8'000'000), 1e-12);
    EXPECT_NEAR(1.08504, b.bestFullAmountAsk(4'000'000), 1e-12);
    EXPECT_TRUE(b.bestFullAmountAskLp(4'000'000) == 0);
    EXPECT_NEAR(1.08502, b.bestFullAmountAsk(1'000'000), 1e-12);
    EXPECT_TRUE(std::isnan(b.bestFullAmountAsk(20'000'000)));
    EXPECT_TRUE(b.bestFullAmountAskLp(20'000'000) == -1);
}

TEST(clearingAnLpRemovesItEverywhere) {
    FxTierBook b = tierBook();
    b.clear(0);
    EXPECT_NEAR(1.08499, b.bestBid(), 1e-12);
    EXPECT_NEAR(1.08503, b.bestAsk(), 1e-12);
    double cost = b.sweepBuyCost(4'000'000);
    double expected = 2'000'000 * 1.08503 + 2'000'000 * 1.08505;
    EXPECT_NEAR(expected, cost, 1e-4);
    b.clear(1);
    EXPECT_TRUE(std::isnan(b.bestBid()));
    EXPECT_TRUE(std::isnan(b.sweepBuyCost(1)));
}

TEST(fullAmountRespectsTierOrderWithinAnLp) {
    FxTierBook b(1, 3);
    b.tier(0, false, 0, 1.0001, 1'000'000);
    b.tier(0, false, 1, 1.0003, 5'000'000);
    b.tier(0, false, 2, 1.0006, 20'000'000);
    b.tierCount(0, false, 3);
    EXPECT_NEAR(1.0001, b.bestFullAmountAsk(500'000), 1e-12);
    EXPECT_NEAR(1.0003, b.bestFullAmountAsk(2'000'000), 1e-12);
    EXPECT_NEAR(1.0006, b.bestFullAmountAsk(19'000'000), 1e-12);
}

TEST(outOfRangeTierOrLpFailsLoudlyInsteadOfCorruptingANeighbor) {
    FxTierBook b(2, 2);
    EXPECT_THROWS(b.tier(0, true, 2, 1.0, 1'000'000));
    EXPECT_THROWS(b.tier(0, true, -1, 1.0, 1'000'000));
    EXPECT_THROWS(b.tier(2, true, 0, 1.0, 1'000'000));
    EXPECT_TRUE(std::isnan(b.bestBid()));
}

TEST(nanAndZeroTiersNeverWinASweep) {
    FxTierBook b(2, 2);
    b.tier(0, false, 0, std::nan(""), 5'000'000);
    b.tierCount(0, false, 1);
    b.tier(1, false, 0, 1.08505, 5'000'000);
    b.tierCount(1, false, 1);
    EXPECT_NEAR(5'000'000 * 1.08505, b.sweepBuyCost(5'000'000), 1e-4);
    std::vector<double> plan(2, 0.0);
    b.sweepPlan(true, 1'000'000, plan);
    EXPECT_NEAR(0, plan[0], 1e-12);
    EXPECT_NEAR(1'000'000, plan[1], 1e-12);
    b.tier(0, false, 0, 1.08000, std::nan(""));
    b.tierCount(0, false, 1);
    EXPECT_NEAR(5'000'000 * 1.08505, b.sweepBuyCost(5'000'000), 1e-4);
    b.tier(0, false, 0, 1.08000, 0);
    b.tierCount(0, false, 1);
    EXPECT_NEAR(5'000'000 * 1.08505, b.sweepBuyCost(5'000'000), 1e-4);
    b.tier(0, false, 0, 0.0, 5'000'000);
    b.tierCount(0, false, 1);
    EXPECT_NEAR(5'000'000 * 1.08505, b.sweepBuyCost(5'000'000), 1e-4);
}

TEST(topOfBookUsesTheFrontierNotBlindTierZero) {
    FxTierBook b(1, 2);
    b.tier(0, true, 0, std::nan(""), 1'000'000);
    b.tier(0, true, 1, 1.08498, 5'000'000);
    b.tierCount(0, true, 2);
    b.tier(0, false, 0, 0.0, 1'000'000);
    b.tier(0, false, 1, 1.08504, 5'000'000);
    b.tierCount(0, false, 2);
    EXPECT_NEAR(1.08498, b.bestBid(), 1e-12);
    EXPECT_NEAR(1.08504, b.bestAsk(), 1e-12);
}

TEST(zeroClipIsNotAQuoteRequest) {
    FxTierBook b = tierBook();
    EXPECT_TRUE(std::isnan(b.fullAmountPrice(0, true, 0)));
    EXPECT_TRUE(std::isnan(b.fullAmountPrice(0, true, -5)));
    EXPECT_TRUE(std::isnan(b.bestFullAmountAsk(0)));
}

TEST(perLpFullAmountPriceIsThePublicRoutingPrimitive) {
    FxTierBook b = tierBook();
    EXPECT_NEAR(1.08502, b.fullAmountPrice(0, true, 1'000'000), 1e-12);
    EXPECT_NEAR(1.08504, b.fullAmountPrice(0, true, 4'000'000), 1e-12);
    EXPECT_TRUE(std::isnan(b.fullAmountPrice(2, true, 1'000'000)));
    EXPECT_TRUE(std::isnan(b.fullAmountPrice(0, true, 50'000'000)));
}

// -------------------------------------------------- LpScorecard / LpRouter

namespace {
constexpr long long MS = 1'000'000LL;

FxTierBook twoLpBook() {
    FxTierBook b(2, 2);
    b.tier(0, false, 0, 1.08502, 5'000'000);
    b.tierCount(0, false, 1);
    b.tier(0, true, 0, 1.08499, 5'000'000);
    b.tierCount(0, true, 1);
    b.tier(1, false, 0, 1.08504, 5'000'000);
    b.tierCount(1, false, 1);
    b.tier(1, true, 0, 1.08497, 5'000'000);
    b.tierCount(1, true, 1);
    return b;
}
}  // namespace

TEST(rejectRateTracksRecentBehavior) {
    LpScorecard c(2, 0.5, 100 * MS);
    EXPECT_NEAR(0, c.rejectRate(0), 1e-12);
    c.onFill(0, true, 1.08502, 1.08501, MS);
    EXPECT_NEAR(0, c.rejectRate(0), 1e-12);
    c.onReject(0, true, 1.08501, 0, 2 * MS);
    EXPECT_NEAR(0.5, c.rejectRate(0), 1e-12);
    c.onReject(0, true, 1.08501, 0, 2 * MS);
    EXPECT_NEAR(0.75, c.rejectRate(0), 1e-12);
    c.onFill(0, true, 1.08502, 1.08501, MS);
    EXPECT_NEAR(0.375, c.rejectRate(0), 1e-12);
    EXPECT_TRUE(c.attempts(0) == 4);
    EXPECT_TRUE(c.fills(0) == 2);
    EXPECT_TRUE(c.rejects(0) == 2);
    EXPECT_TRUE(c.attempts(1) == 0);
}

TEST(postRejectMarkoutMaturesAtTheHorizon) {
    LpScorecard c(1, 1.0, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    c.onMid(1.08505, 50 * MS);
    EXPECT_NEAR(0, c.postRejectMarkout(0), 1e-12);
    c.onMid(1.08510, 100 * MS);
    EXPECT_NEAR(0.00010, c.postRejectMarkout(0), 1e-9);
    c.onReject(0, false, 1.08510, 200 * MS, MS);
    c.onMid(1.08495, 300 * MS);
    EXPECT_NEAR(0.00015, c.postRejectMarkout(0), 1e-9);
}

TEST(rejectBurstsAreSampledNotOverwritten) {
    LpScorecard c(1, 1.0, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    c.onReject(0, true, 1.08508, 60 * MS, MS);
    c.onMid(1.08512, 160 * MS);
    EXPECT_TRUE(c.maturedMarkouts() == 2);
    EXPECT_NEAR(0.00004, c.postRejectMarkout(0), 1e-9);
}

TEST(nanReferenceMidsNeverStartAMarkout) {
    LpScorecard c(1, 1.0, 100 * MS);
    c.onReject(0, true, std::nan(""), 0, MS);
    EXPECT_TRUE(c.rejects(0) == 1);
    c.onMid(1.08510, 200 * MS);
    EXPECT_TRUE(c.maturedMarkouts() == 0);
    EXPECT_NEAR(0.0, c.postRejectMarkout(0), 1e-12);
    c.onReject(0, true, 1.08500, 300 * MS, MS);
    c.onMid(1.08505, 400 * MS);
    EXPECT_NEAR(0.00005, c.postRejectMarkout(0), 1e-9);
}

TEST(nanMidsNeverPoisonTheMarkout) {
    LpScorecard c(1, 1.0, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    c.onMid(std::nan(""), 200 * MS);
    EXPECT_TRUE(c.maturedMarkouts() == 0);
    c.onMid(1.08510, 300 * MS);
    EXPECT_NEAR(0.00010, c.postRejectMarkout(0), 1e-9);
    EXPECT_TRUE(c.maturedMarkouts() == 1);
}

TEST(poisonedOrUnquotedExpectedPricesNeverCaptureTheRouter) {
    FxTierBook b = twoLpBook();
    LpScorecard c(2, 1.0, 100 * MS);
    LpRouter r(b, c, 1.0);
    b.tierCount(0, false, 0);
    EXPECT_TRUE(r.route(true, 1'000'000) == 1);
    EXPECT_NEAR(1.08504, r.lastQuotedPrice(), 1e-12);
}

TEST(effectiveSpreadAndHoldAreEwmas) {
    LpScorecard c(1, 1.0, 100 * MS);
    c.onFill(0, true, 1.08503, 1.08501, 5 * MS);
    EXPECT_NEAR(0.00002, c.effectiveSpread(0), 1e-9);
    EXPECT_NEAR(5 * MS, c.avgHoldNanos(0), 1e-6);
    c.onFill(0, false, 1.08499, 1.08501, 3 * MS);
    EXPECT_NEAR(0.00002, c.effectiveSpread(0), 1e-9);
    EXPECT_NEAR(3 * MS, c.avgHoldNanos(0), 1e-6);
}

TEST(cleanBooksRouteToTheTightestQuote) {
    FxTierBook b = twoLpBook();
    LpScorecard c(2);
    LpRouter r(b, c, 0.25);
    EXPECT_TRUE(r.route(true, 1'000'000) == 0);
    EXPECT_NEAR(1.08502, r.lastQuotedPrice(), 1e-12);
    EXPECT_NEAR(1.08502, r.lastExpectedPrice(), 1e-12);
    EXPECT_TRUE(r.route(false, 1'000'000) == 0);
}

TEST(rejectyLpLosesDespiteTheTighterQuote) {
    FxTierBook b = twoLpBook();
    LpScorecard c(2, 1.0, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    c.onMid(1.08530, 100 * MS);
    LpRouter r(b, c, 1.0);
    EXPECT_TRUE(r.route(true, 1'000'000) == 1);
    EXPECT_NEAR(1.08504, r.lastQuotedPrice(), 1e-12);
}

TEST(rejectRateCapVetoesOutright) {
    FxTierBook b = twoLpBook();
    LpScorecard c(2, 1.0, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    LpRouter r(b, c, 0.25);
    EXPECT_TRUE(r.route(true, 1'000'000) == 1);
    EXPECT_TRUE(r.vetoCount() > 0);
    c.onReject(1, true, 1.08500, 0, MS);
    EXPECT_TRUE(r.route(true, 1'000'000) == -1);
    EXPECT_TRUE(std::isnan(r.lastQuotedPrice()));
}

TEST(holdTimeIsPricedLikeLatencyWhenUrgencyIsSet) {
    FxTierBook b(2, 1);
    for (int lp = 0; lp < 2; lp++) {
        b.tier(lp, false, 0, 1.08502, 5'000'000);
        b.tierCount(lp, false, 1);
    }
    LpScorecard c(2, 1.0, 100 * MS);
    c.onFill(0, true, 1.08502, 1.08501, 50 * MS);
    c.onFill(1, true, 1.08502, 1.08501, MS);
    LpRouter plain(b, c, 1.0);
    EXPECT_TRUE(plain.route(true, 1'000'000) == 0);
    LpRouter urgent(b, c, 1.0, 1.0);
    EXPECT_TRUE(urgent.route(true, 1'000'000) == 1);
    EXPECT_TRUE(urgent.lastExpectedPrice() > urgent.lastQuotedPrice());
}

TEST(firstMarkoutSeedsAtFullStrengthNotFivePercent) {
    LpScorecard c(2, 0.05, 100 * MS);
    c.onReject(0, true, 1.08500, 0, MS);
    c.onMid(1.08530, 200 * MS);
    EXPECT_NEAR(0.00030, c.postRejectMarkout(0), 1e-12);
    c.onReject(0, true, 1.08530, 300 * MS, MS);
    c.onMid(1.08540, 500 * MS);
    EXPECT_NEAR(0.00030 + 0.05 * (0.00010 - 0.00030), c.postRejectMarkout(0), 1e-12);
}

TEST(routerRespectsClipSizeAgainstTiers) {
    FxTierBook b = twoLpBook();
    LpScorecard c(2);
    LpRouter r(b, c, 0.5);
    EXPECT_TRUE(r.route(true, 20'000'000) == -1);
    EXPECT_TRUE(r.route(true, 5'000'000) == 0);
}

QTEST_MAIN()
