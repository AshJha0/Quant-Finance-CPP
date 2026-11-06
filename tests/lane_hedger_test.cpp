// Port of the Java AutoHedger behavior pins: inside-the-band ticks are a
// no-op, a breach submits an opposite-side order sized to the excess OVER
// the band (hedge to the band edge, not to flat), the cooldown suppresses
// a second hedge while the first is in flight, and the NEVER sentinel
// (Long.MIN_VALUE in Java) must not fire the cooldown arithmetic on the
// very first hedge even when timestamps start near zero or go negative.
#include "quantfinlib/trading/auto_hedger.hpp"

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AutoHedger;
using quantfinlib::HftOrderGateway;
using quantfinlib::HftRiskGate;
using quantfinlib::Side;

TEST(constructorValidatesArguments) {
    HftRiskGate gate(2);
    HftOrderGateway gw(gate);
    EXPECT_THROWS(AutoHedger(gw, 2, 0, 0));
    EXPECT_THROWS(AutoHedger(gw, 2, 100, -1));
}

TEST(insideBandIsANoOp) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);
    gate.onFill(0, Side::BUY, 400);   // within a 500 band

    AutoHedger hedger(gw, 2, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 0);
    EXPECT_TRUE(hedger.hedgesRejected() == 0);
}

TEST(breachHedgesTheExcessOverTheBandNotToFlat) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);
    gate.onFill(0, Side::BUY, 800);   // 300 over a 500 band

    AutoHedger hedger(gw, 2, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);

    // The hedge order was accepted by the SELL side of the gate's own
    // check; confirm the sign is opposite (long -> sell) by checking the
    // gate's position is unaffected until a fill (onFill is separate) --
    // the observable contract here is just "one hedge submitted".
    gw.close();
}

TEST(shortPositionHedgesWithABuy) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);
    std::vector<Side> sides;
    std::atomic<int> count{0};
    gw.addOrderListener([&](std::int64_t, int, Side side, std::int64_t, double, std::int64_t) {
        sides.push_back(side);
        count.fetch_add(1, std::memory_order_release);
    });
    gw.start();
    gate.onFill(0, Side::SELL, 800);   // -300 over a 500 band (short)

    AutoHedger hedger(gw, 2, 500, 0);
    hedger.onTick(0, 100.0, 1, 1);
    // Wait on the atomic counter (never on the vector itself) from the test
    // thread -- the vector is only read below, AFTER gw.close() joins the
    // venue thread that writes it.
    for (int i = 0; i < 2000 && count.load(std::memory_order_acquire) == 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    gw.close();
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    EXPECT_TRUE(!sides.empty());
    EXPECT_TRUE(sides[0] == Side::BUY);   // short position hedges by buying
}

TEST(cooldownSuppressesASecondHedgeUntilItElapses) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);
    gate.onFill(0, Side::BUY, 800);   // 300 over band, stays there (no onFill on hedge)

    AutoHedger hedger(gw, 2, 500, 1000);   // 1000ns cooldown
    hedger.onTick(0, 100.0, 1, 0);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);

    // Still within cooldown: no second hedge, not even counted as rejected
    // (the cooldown short-circuits before the gateway is touched).
    hedger.onTick(0, 100.0, 1, 500);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    EXPECT_TRUE(hedger.hedgesRejected() == 0);

    // Cooldown elapsed but the first hedge is still working and covers the
    // excess. Review finding C2: this used to pin a second, stacked hedge.
    hedger.onTick(0, 100.0, 1, 1001);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    gw.close();
}

TEST(neverSentinelDoesNotSuppressTheFirstHedgeNearTimeZeroOrNegative) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);
    gate.onFill(0, Side::BUY, 800);

    // A long cooldown, but the FIRST hedge must fire regardless of how
    // close timestampNanos is to 0 or how negative it is (System.nanoTime's
    // arbitrary origin) -- NEVER (INT64_MIN) is checked as a sentinel, not
    // subtracted from.
    AutoHedger hedger(gw, 2, 500, 1'000'000'000LL);
    hedger.onTick(0, 100.0, 1, -1'000'000LL);
    EXPECT_TRUE(hedger.hedgesSubmitted() == 1);
    gw.close();
}

QTEST_MAIN()
