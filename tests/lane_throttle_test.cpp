// Port of the Java OrderThrottle behavior pins: starts full (an opening
// burst is free), refills continuously between acquires, never exceeds the
// burst depth, tryAcquire/nanosUntilAvailable agree, and the 1e-9 slack
// grants at most sub-nanosecond-early permits (never extra whole ones).
#include "quantfinlib/trading/order_throttle.hpp"

#include "qtest.hpp"

using quantfinlib::OrderThrottle;

TEST(constructorValidatesArguments) {
    EXPECT_THROWS(OrderThrottle(0, 10));
    EXPECT_THROWS(OrderThrottle(-5, 10));
    EXPECT_THROWS(OrderThrottle(100, 0));
}

TEST(startsFullAllowingAnOpeningBurst) {
    OrderThrottle t(1000, 5);   // 1000/sec, burst 5
    for (int i = 0; i < 5; i++) {
        EXPECT_TRUE(t.tryAcquire(0));
    }
    // The 6th immediate call at the same instant must fail (bucket empty).
    EXPECT_TRUE(!t.tryAcquire(0));
    EXPECT_TRUE(t.acquiredCount() == 5);
    EXPECT_TRUE(t.throttledCount() == 1);
}

TEST(refillsContinuouslyWithElapsedTime) {
    OrderThrottle t(1000, 1);   // 1 token/ms, burst 1
    EXPECT_TRUE(t.tryAcquire(0));
    EXPECT_TRUE(!t.tryAcquire(100));   // 100ns later: nowhere near a full token
    // After 1ms (1e6 ns) exactly one token has regenerated.
    EXPECT_TRUE(t.tryAcquire(1'000'000));
    EXPECT_TRUE(!t.tryAcquire(1'000'000));
}

TEST(neverExceedsBurstDepthEvenAfterALongIdlePeriod) {
    OrderThrottle t(1000, 3);   // burst 3
    // A huge elapsed gap must cap tokens at burst, not overflow past it.
    EXPECT_TRUE(t.tryAcquire(0));
    EXPECT_TRUE(t.tryAcquire(0));
    EXPECT_TRUE(t.tryAcquire(0));
    EXPECT_TRUE(!t.tryAcquire(0));
    // Wait a very long time (idle spell banks at most one burst).
    bool ok1 = t.tryAcquire(1'000'000'000'000LL);
    bool ok2 = t.tryAcquire(1'000'000'000'000LL);
    bool ok3 = t.tryAcquire(1'000'000'000'000LL);
    bool ok4 = t.tryAcquire(1'000'000'000'000LL);
    EXPECT_TRUE(ok1 && ok2 && ok3);
    EXPECT_TRUE(!ok4);   // capped at burst=3, the 4th must still fail
}

TEST(nanosUntilAvailableAgreesWithTryAcquire) {
    OrderThrottle t(1000, 1);   // 1 token per 1e6 ns
    EXPECT_TRUE(t.tryAcquire(0));
    std::int64_t wait = t.nanosUntilAvailable(0);
    EXPECT_TRUE(wait > 0);
    EXPECT_TRUE(!t.tryAcquire(wait - 1));
    EXPECT_TRUE(t.tryAcquire(wait));
}

TEST(zeroWaitWhenATokenIsAlreadyAvailable) {
    OrderThrottle t(1000, 5);
    EXPECT_TRUE(t.nanosUntilAvailable(0) == 0);
}

QTEST_MAIN()
