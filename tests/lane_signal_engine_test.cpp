// Port of the Java SignalEngine behavior pins: the gap gate (a one-sided
// or non-dealable quote must not update any estimator or poison the
// baseline), first-quote seeding (momentum/vol/gap start at 0, spread and
// depth seed immediately), the re-seed after a gap, and the composite
// alpha()'s zero-weight/unseeded degenerate cases.
#include "quantfinlib/microstructure/signal_engine.hpp"

#include <cmath>

#include "qtest.hpp"

using quantfinlib::SignalEngine;

TEST(constructorValidatesArguments) {
    EXPECT_THROWS(SignalEngine(0));
    EXPECT_THROWS(SignalEngine::Config(0, 1, 2, 1, 1, 1, 1, 1, 1));
    // fast >= slow half-life is invalid.
    EXPECT_THROWS(SignalEngine::Config(1, 10, 10, 1, 1, 1, 1, 1, 1));
    EXPECT_THROWS(SignalEngine::Config(1, 1, 2, 1, 1, -1, 1, 1, 1));
}

TEST(firstTwoSidedQuoteSeedsEveryEstimatorAtZeroMomentumAndVol) {
    SignalEngine eng(2);
    eng.onQuote(0, 99.0, 100, 101.0, 100, 1'000'000);
    EXPECT_NEAR(eng.microprice(0), 100.0, 1e-9);   // balanced sizes -> mid
    EXPECT_TRUE(eng.momentum(0) == 0);
    EXPECT_TRUE(eng.volPerSqrtSecond(0) == 0);
    EXPECT_NEAR(eng.spread(0), 2.0, 1e-9);
    EXPECT_NEAR(eng.topDepth(0), 200.0, 1e-9);
    EXPECT_TRUE(eng.quoteCount() == 1);
}

TEST(oneSidedOrNonDealableQuoteIsAGapNoUpdateNoPoison) {
    SignalEngine eng(1);
    eng.onQuote(0, 99.0, 100, 101.0, 100, 0);
    double micro0 = eng.microprice(0);

    // Zero size on one side: a gap, must not touch anything.
    eng.onQuote(0, 99.0, 0, 101.0, 100, 1'000'000);
    EXPECT_NEAR(eng.microprice(0), micro0, 1e-12);

    // Non-positive/NaN price: also a gap.
    eng.onQuote(0, 0.0, 100, 101.0, 100, 2'000'000);
    EXPECT_NEAR(eng.microprice(0), micro0, 1e-12);

    // A gap resets hasMid, so spreadBps must go back to NaN until reseeded.
    EXPECT_TRUE(std::isnan(eng.spreadBps(0)));
    EXPECT_TRUE(eng.quoteCount() == 3);
}

TEST(gapReseedsBaselinesOnTheNextTwoSidedQuote) {
    SignalEngine eng(1);
    eng.onQuote(0, 99.0, 100, 101.0, 100, 0);
    eng.onQuote(0, 0.0, 100, 101.0, 100, 1'000'000);   // gap
    // Re-seed: momentum/vol reset to the fresh baseline (0), not carried
    // over from before the gap.
    eng.onQuote(0, 199.0, 100, 201.0, 100, 2'000'000);
    EXPECT_TRUE(eng.momentum(0) == 0);
    EXPECT_TRUE(eng.volPerSqrtSecond(0) == 0);
    EXPECT_NEAR(eng.microprice(0), 200.0, 1e-9);
}

TEST(momentumIsPositiveWhenFastLeadsSlowUpward) {
    SignalEngine::Config cfg(500'000'000LL, 100'000'000LL, 2'000'000'000LL, 10'000'000'000LL,
                             10'000'000'000LL, 1, 1, 1, 1);
    SignalEngine eng(1, cfg);
    std::int64_t t = 0;
    eng.onQuote(0, 99.0, 100, 101.0, 100, t);
    for (int i = 0; i < 20; i++) {
        t += 50'000'000LL;   // 50ms steps
        eng.onQuote(0, 99.0 + i, 100, 101.0 + i, 100, t);
    }
    EXPECT_TRUE(eng.momentum(0) > 0);   // sustained uptrend -> fast > slow
}

TEST(alphaIsZeroWhenAllCompositeWeightsAreZero) {
    SignalEngine::Config cfg(500'000'000LL, 2'000'000'000LL, 20'000'000'000LL, 10'000'000'000LL,
                             10'000'000'000LL, 0, 0, 0, 0);
    SignalEngine eng(1, cfg);
    eng.onQuote(0, 99.0, 100, 101.0, 100, 0);
    eng.onQuote(0, 99.5, 150, 101.5, 50, 1'000'000'000LL);
    EXPECT_TRUE(eng.alpha(0) == 0);
}

TEST(tradeCountIncrementsIndependentlyOfQuotes) {
    SignalEngine eng(1);
    eng.onTrade(0, true, 100, 1);
    eng.onTrade(0, false, 50, 2);
    EXPECT_TRUE(eng.tradeCount() == 2);
    EXPECT_TRUE(eng.quoteCount() == 0);
    // Signed aggressor imbalance must be in [-1, 1].
    EXPECT_TRUE(eng.tradeImbalance(0) >= -1.0 && eng.tradeImbalance(0) <= 1.0);
}

QTEST_MAIN()
