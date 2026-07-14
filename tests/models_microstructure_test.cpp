// Hand-pinned regressions for the microstructure model classes: Lee-Ready
// trade classification, hidden-liquidity/iceberg inference, VPIN bucket
// arithmetic (including the long-overflow clamp), flow signals, jump-robust
// (bipower) volatility on pure diffusion, Hawkes stability enforcement,
// EwmaCovariance's PSD guarantee, Avellaneda-Stoikov's units contract,
// queue-position/fill-probability composition, and bar-only liquidity
// measures. Derived from the class-level Javadoc contracts in the Java
// source (com.quantfinlib.microstructure), since these are streaming,
// deterministic models with no external test-vector dependency.
#include "quantfinlib/microstructure/avellaneda_stoikov.hpp"
#include "quantfinlib/microstructure/ewma_covariance.hpp"
#include "quantfinlib/microstructure/fill_probability_model.hpp"
#include "quantfinlib/microstructure/flow_signals.hpp"
#include "quantfinlib/microstructure/hawkes_intensity.hpp"
#include "quantfinlib/microstructure/hidden_liquidity_detector.hpp"
#include "quantfinlib/microstructure/jump_robust_volatility.hpp"
#include "quantfinlib/microstructure/liquidity_measures.hpp"
#include "quantfinlib/microstructure/queue_position_estimator.hpp"
#include "quantfinlib/microstructure/trade_classifier.hpp"
#include "quantfinlib/microstructure/vpin.hpp"
#include "quantfinlib/risk/java_random.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include "qtest.hpp"

using namespace quantfinlib;

// ------------------------------------------------------------------
// TradeClassifier (Lee-Ready)
// ------------------------------------------------------------------

TEST(quoteRuleClassifiesAtOrThroughTheQuotes) {
    TradeClassifier c;
    c.onQuote(99.0, 101.0);
    EXPECT_TRUE(c.classify(101.0) == TradeClassifier::BUY);   // at the ask
    EXPECT_TRUE(c.classify(102.0) == TradeClassifier::BUY);   // through the ask
    EXPECT_TRUE(c.classify(99.0) == TradeClassifier::SELL);   // at the bid
    EXPECT_TRUE(c.classify(98.0) == TradeClassifier::SELL);   // through the bid
}

TEST(quoteRuleLeansOnTheMidBetweenQuotes) {
    TradeClassifier c;
    c.onQuote(99.0, 101.0);
    EXPECT_TRUE(c.classify(100.5) == TradeClassifier::BUY);   // above mid
    EXPECT_TRUE(c.classify(99.5) == TradeClassifier::SELL);   // below mid
}

TEST(tickTestAppliesExactlyAtTheMidOrWithoutAQuote) {
    TradeClassifier c;
    // No quote at all: falls straight to the tick test.
    EXPECT_TRUE(c.classify(100.0) == TradeClassifier::UNKNOWN);  // no prior trade either
    EXPECT_TRUE(c.classify(101.0) == TradeClassifier::BUY);      // uptick
    EXPECT_TRUE(c.classify(100.0) == TradeClassifier::SELL);     // downtick
    EXPECT_TRUE(c.classify(100.0) == TradeClassifier::SELL);     // zero-tick repeats

    TradeClassifier c2;
    c2.onQuote(99.0, 101.0);
    c2.classify(101.0);   // BUY at the ask, seeds the tick test
    c2.onQuote(99.0, 101.0);
    // Exactly at mid: quote rule is UNKNOWN, so the tick test decides --
    // downtick from the previous trade price (101 -> 100) is a SELL.
    EXPECT_TRUE(c2.classify(100.0) == TradeClassifier::SELL);
}

TEST(nonDealablePricesClassifyAsUnknown) {
    TradeClassifier c;
    c.onQuote(99.0, 101.0);
    EXPECT_TRUE(c.classify(0.0) == TradeClassifier::UNKNOWN);
    EXPECT_TRUE(c.classify(-5.0) == TradeClassifier::UNKNOWN);
    EXPECT_TRUE(c.classify(std::numeric_limits<double>::infinity()) == TradeClassifier::UNKNOWN);
}

TEST(isBuyAggressorFallsBackToLastKnownSide) {
    TradeClassifier c;
    c.onQuote(99.0, 101.0);
    EXPECT_TRUE(c.isBuyAggressor(101.0));  // classifies BUY directly
    // Now query at exactly the mid with no quote change and no tick history
    // that disambiguates -- exercise via a fresh instance without a quote.
    TradeClassifier c2;
    EXPECT_TRUE(!c2.isBuyAggressor(100.0));  // UNKNOWN maps to lastClassification (UNKNOWN != BUY)
}

// ------------------------------------------------------------------
// HiddenLiquidityDetector (covers "iceberg" detection too)
// ------------------------------------------------------------------

TEST(hiddenLiquidityDetectsAPrintLargerThanDisplayed) {
    HiddenLiquidityDetector d(4, 0.5);
    d.onDisplayed(0, 100);
    EXPECT_NEAR(d.hiddenMultiplier(0), 1.0, 1e-12);  // no evidence yet
    EXPECT_TRUE(!d.isIceberg(0));

    d.onExecution(0, 300);  // 3x the displayed size -- necessarily hidden size behind it
    EXPECT_NEAR(d.hiddenMultiplier(0), 3.0, 1e-12);  // seeds directly from the first observation
    EXPECT_TRUE(d.isIceberg(0));
    EXPECT_NEAR(d.estimatedTrueDepth(0), 100 * 3.0, 1e-12);

    d.onExecution(0, 100);  // exactly the displayed size: not a hidden event
    EXPECT_TRUE(d.refillObservations(0) == 1);

    d.onLevelCleared(0);
    d.onExecution(0, 50);  // displayed is now 0: no hidden inference possible
    EXPECT_TRUE(d.refillObservations(0) == 1);
}

// ------------------------------------------------------------------
// Vpin
// ------------------------------------------------------------------

TEST(vpinAveragesCompletedBucketImbalances) {
    Vpin v(100, 3);
    EXPECT_TRUE(std::isnan(v.vpin()));
    v.onTrade(60, true);
    v.onTrade(40, true);  // completes bucket 1: all buys, imbalance 1.0
    EXPECT_NEAR(v.vpin(), 1.0, 1e-12);
    v.onTrade(50, true);
    v.onTrade(50, false);  // completes bucket 2: balanced, imbalance 0.0
    EXPECT_NEAR(v.vpin(), 0.5, 1e-12);
    EXPECT_TRUE(v.bucketsCompleted() == 2);
    EXPECT_TRUE(!v.ready());
}

TEST(vpinSplitsATradeAcrossBucketBoundaries) {
    Vpin v(100, 2);
    v.onTrade(250, true);  // spans 2.5 buckets: bucket1(100 buy)=1.0, bucket2(100 buy)=1.0, remainder 50 buy open
    EXPECT_TRUE(v.bucketsCompleted() == 2);
    EXPECT_NEAR(v.vpin(), 1.0, 1e-12);
    EXPECT_TRUE(v.ready());
}

TEST(vpinHugeTradeClampsWithoutOverflow) {
    // A trade far larger than the window can represent must not corrupt
    // `filled` via overflow -- it saturates at the window size.
    Vpin v(1, 5);
    v.onTrade(1'000'000, true);
    EXPECT_TRUE(v.bucketsCompleted() == 5);
    EXPECT_TRUE(v.ready());
    EXPECT_NEAR(v.vpin(), 1.0, 1e-12);
}

// ------------------------------------------------------------------
// FlowSignals
// ------------------------------------------------------------------

TEST(flowSignalsQueueImbalanceAndOfiSigns) {
    FlowSignals f(500'000'000LL);
    f.onQuote(99.0, 100, 101.0, 100, 0);
    EXPECT_NEAR(f.queueImbalance(), 0.0, 1e-12);
    // Bid increases (buying pressure): OFI should go positive.
    f.onQuote(99.5, 100, 101.0, 100, 1'000'000);
    EXPECT_TRUE(f.ofi() > 0);
    // One-sided quote (a gap) resets queueImbalance to 0 and doesn't book flow.
    f.onQuote(0.0, 0, 101.0, 100, 2'000'000);
    EXPECT_NEAR(f.queueImbalance(), 0.0, 1e-12);
}

TEST(flowSignalsTradeImbalanceSignsWithAggressor) {
    FlowSignals f;
    f.onTrade(true, 100, 0);
    EXPECT_NEAR(f.tradeImbalance(), 1.0, 1e-9);
    f.onTrade(false, 100, 0);  // same instant: no decay
    EXPECT_NEAR(f.tradeImbalance(), 0.0, 1e-9);
}

// ------------------------------------------------------------------
// JumpRobustVolatility
// ------------------------------------------------------------------

TEST(jumpRobustVolatilityDoesNotFlagPureDiffusionAsJumpy) {
    // Gaussian iid returns (the model bipower variation is derived for):
    // no discontinuity, so jumpFraction should stay small and raw/robust
    // should be the same order of magnitude.
    JumpRobustVolatility v(10'000'000'000LL);
    JavaRandom rng(123);
    double sigma = 0.001;
    for (int i = 0; i < 500; i++) {
        v.onReturn(sigma * rng.nextGaussian(), 1'000'000'000LL);
    }
    EXPECT_TRUE(v.samples() == 500);
    double raw = v.rawVolPerSqrtSecond();
    double robust = v.volPerSqrtSecond();
    EXPECT_TRUE(raw > 0 && robust > 0);
    EXPECT_TRUE(robust < raw * 1.5 && robust > raw * 0.5);  // same order of magnitude
    EXPECT_TRUE(v.jumpFraction() < 0.3);                     // pure diffusion is NOT flagged as jumpy
}

TEST(jumpRobustVolatilityFlagsADiscontinuousMove) {
    JumpRobustVolatility v(10'000'000'000LL);
    JavaRandom rng(123);
    double sigma = 0.001;
    for (int i = 0; i < 50; i++) {
        v.onReturn(sigma * rng.nextGaussian(), 1'000'000'000LL);
    }
    v.onReturn(0.20, 1'000'000'000LL);  // one large jump, dwarfing the noise
    EXPECT_TRUE(v.jumpFraction() > 0.3);  // the jump inflates raw far more than bipower
}

TEST(jumpRobustVolatilityBreaksPairingAcrossAGap) {
    JumpRobustVolatility v;
    v.onReturn(0.01, 1'000'000'000LL);
    v.onReturn(std::nan(""), 1'000'000'000LL);  // gap: drops the sample, breaks pairing
    // Next return only updates the raw estimator, not bipower (no NaN leaks in).
    v.onReturn(0.01, 1'000'000'000LL);
    EXPECT_TRUE(std::isfinite(v.volPerSqrtSecond()));
    EXPECT_TRUE(std::isfinite(v.rawVolPerSqrtSecond()));
}

// ------------------------------------------------------------------
// HawkesIntensity
// ------------------------------------------------------------------

TEST(hawkesConstructorRejectsExplosiveBranchingRatio) {
    // beta = ln2 / 2s ~= 0.3466/s; excitation 0.4 gives branching ratio > 1.
    EXPECT_THROWS(HawkesIntensity(2.0, 0.4, 2'000'000'000LL));
    // A stable configuration must NOT throw.
    HawkesIntensity stable(2.0, 0.1, 2'000'000'000LL);
    EXPECT_NEAR(stable.intensity(0), 2.0, 1e-12);  // no events yet: baseline only
}

TEST(hawkesBurstScoreDecaysToZero) {
    HawkesIntensity h(2.0, 0.1, 2'000'000'000LL);
    h.onEvent(0);
    double burstNow = h.burstScore(0);
    EXPECT_TRUE(burstNow > 0);
    double burstLater = h.burstScore(20'000'000'000LL);  // 10 half-lives later
    EXPECT_TRUE(burstLater < burstNow);
    EXPECT_TRUE(burstLater >= 0);
}

// ------------------------------------------------------------------
// EwmaCovariance (PSD property)
// ------------------------------------------------------------------

TEST(ewmaCovariancePortfolioVarianceIsNeverNegative) {
    EwmaCovariance cov(3, 0.9);
    // Feed a handful of correlated-ish return vectors.
    std::vector<std::vector<double>> samples = {
            {0.01, 0.02, -0.01}, {-0.02, -0.01, 0.03}, {0.015, 0.005, -0.02},
            {-0.01, 0.0, 0.01}, {0.02, 0.03, -0.015}};
    for (const auto& s : samples) {
        cov.onReturns(s);
    }
    // Any weight vector must produce a non-negative portfolio variance --
    // the PSD guarantee from full-vector rank-1 outer-product updates.
    std::vector<std::vector<double>> weightSets = {
            {1, 0, 0}, {0, 1, -1}, {0.5, 0.3, -0.8}, {2.0, -1.0, -1.0}};
    for (const auto& w : weightSets) {
        EXPECT_TRUE(cov.portfolioVariance(w) >= -1e-12);
    }
    // A non-finite sample is dropped whole.
    EwmaCovariance cov2(2, 0.9);
    cov2.onReturns({0.01, std::nan("")});
    EXPECT_TRUE(cov2.samples() == 0);
    EXPECT_NEAR(cov2.variance(0), 0.0, 0.0);
}

TEST(ewmaCovarianceMarginalContributionsSumToOne) {
    EwmaCovariance cov(2, 0.9);
    cov.onReturns({0.01, 0.02});
    cov.onReturns({-0.01, 0.015});
    cov.onReturns({0.02, -0.005});
    std::vector<double> w = {0.6, 0.4};
    std::vector<double> out(2);
    double total = cov.marginalContribution(w, out);
    EXPECT_TRUE(total > 0);
    EXPECT_NEAR(out[0] + out[1], 1.0, 1e-9);
}

// ------------------------------------------------------------------
// AvellanedaStoikov (units contract)
// ------------------------------------------------------------------

TEST(avellanedaStoikovShadesLongInventoryDownward) {
    AvellanedaStoikov as(0.1, 1.5);
    double mid = 100.0;
    double priceVarPerSec = std::pow(mid * 0.0002, 2);  // (mid*volPerSqrtSec)^2
    double horizon = 3600;  // 1 hour
    double rLong = as.reservationPrice(mid, 10, priceVarPerSec, horizon);
    double rFlat = as.reservationPrice(mid, 0, priceVarPerSec, horizon);
    double rShort = as.reservationPrice(mid, -10, priceVarPerSec, horizon);
    EXPECT_NEAR(rFlat, mid, 1e-12);
    EXPECT_TRUE(rLong < mid);   // long inventory shades down (want to sell)
    EXPECT_TRUE(rShort > mid);  // short inventory shades up
    // Symmetric around mid for symmetric inventory.
    EXPECT_NEAR((mid - rLong), (rShort - mid), 1e-9);
}

TEST(avellanedaStoikovSpreadNeverBelowTheLiquidityFloor) {
    AvellanedaStoikov as(0.1, 1.5);
    double floor = std::log1p(0.1 / 1.5) / 0.1;
    // Garbage/zero variance: spread collapses exactly to the liquidity floor.
    EXPECT_NEAR(as.optimalHalfSpread(-1.0, 100), floor, 1e-12);
    EXPECT_NEAR(as.optimalHalfSpread(std::nan(""), 100), floor, 1e-12);
    // Positive variance widens beyond the floor.
    EXPECT_TRUE(as.optimalHalfSpread(1e-6, 3600) > floor);
    EXPECT_THROWS(AvellanedaStoikov(0, 1.5));
    EXPECT_THROWS(AvellanedaStoikov(0.1, 0));
}

// ------------------------------------------------------------------
// QueuePositionEstimator + FillProbabilityModel
// ------------------------------------------------------------------

TEST(queuePositionEstimatorTracksProRataCancels) {
    QueuePositionEstimator q;
    q.join(1000, 100);  // 1000 ahead, our 100 behind
    EXPECT_NEAR(q.sharesAhead(), 1000, 1e-12);
    q.onTrade(200);  // trades eat the front
    EXPECT_NEAR(q.sharesAhead(), 800, 1e-12);
    // Level shrinks by 400 (cancels among the 800 others ahead of/around us).
    q.onLevelResize(1000 - 400);  // level was 800(others)+100(ours)=900 -> new 600
    EXPECT_TRUE(q.sharesAhead() < 800);
    EXPECT_TRUE(q.queueProgress() > 0 && q.queueProgress() <= 1.0);
    q.close();
    EXPECT_TRUE(!q.active());
}

TEST(fillProbabilityCompositionIsBoundedAndMonotone) {
    double p1 = fill_probability_model::passiveFillProbability(0.5, 0.001, 60, 100, 1000, 100, 5000);
    double p2 = fill_probability_model::passiveFillProbability(2.0, 0.001, 60, 100, 1000, 100, 5000);
    EXPECT_TRUE(p1 >= 0 && p1 <= 1);
    EXPECT_TRUE(p2 >= 0 && p2 <= 1);
    EXPECT_TRUE(p1 > p2);  // closer to the touch fills more often
    EXPECT_NEAR(fill_probability_model::touchProbability(0, 0.001, 60, 100), 1.0, 1e-12);
}

// ------------------------------------------------------------------
// LiquidityMeasures
// ------------------------------------------------------------------

TEST(rollSpreadIsNaNWithoutTheBounceSignature) {
    // A monotone trend has positive autocovariance in price changes: no
    // bounce signature, so Roll's estimator must report NaN, not zero.
    std::vector<double> trending = {100, 101, 102, 103, 104, 105};
    EXPECT_TRUE(std::isnan(liquidity_measures::rollSpread(trending)));
    // A bouncing series (alternating up/down) has negative autocovariance.
    std::vector<double> bouncing = {100, 101, 100, 101, 100, 101};
    double s = liquidity_measures::rollSpread(bouncing);
    EXPECT_TRUE(!std::isnan(s) && s > 0);
}

TEST(amihudIlliquidityThrowsOnZeroVolume) {
    std::vector<double> returns = {0.01, -0.02, 0.03};
    std::vector<double> vol = {1e6, 1e6, 0.0};
    EXPECT_THROWS(liquidity_measures::amihudIlliquidity(returns, vol));
    std::vector<double> volOk = {1e6, 2e6, 0.5e6};
    double illiq = liquidity_measures::amihudIlliquidity(returns, volOk);
    EXPECT_TRUE(illiq > 0);
}

QTEST_MAIN()
