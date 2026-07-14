// Port of the Java GlobalRiskAggregator behavior pins: gross notional is
// summed across every gate's positions x reference price (NaN-safe), a
// breach kills EVERY gate and trips exactly once, recovery is hysteretic
// (resumes only below cap x resumeFraction, not merely below cap), and a
// gate that was ALREADY killed before the trip (an ops hold, a second
// breaker) stays killed after the aggregator's own recovery -- the
// killedBeforeTrip snapshot pin.
#include "quantfinlib/trading/global_risk_aggregator.hpp"

#include <chrono>
#include <thread>

#include "qtest.hpp"

using quantfinlib::GlobalRiskAggregator;
using quantfinlib::HftRiskGate;
using quantfinlib::Side;

namespace {
constexpr std::int64_t POLL_NANOS = 200'000;   // 0.2ms poll for a fast test

template <typename Pred>
bool waitFor(Pred pred, int maxMs = 3000) {
    for (int i = 0; i < maxMs; i++) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}
}  // namespace

TEST(grossNotionalSumsAbsPositionTimesReferencePriceNanSafe) {
    HftRiskGate g0(4), g1(4);
    g0.onFill(0, Side::BUY, 100);
    g0.setReferencePrice(0, 10.0);
    g0.onFill(1, Side::SELL, 50);   // no reference price set: NaN -> contributes 0
    g1.onFill(0, Side::BUY, 20);
    g1.setReferencePrice(0, 5.0);

    GlobalRiskAggregator agg({&g0, &g1}, 1e9, 0.9, POLL_NANOS);
    // 100*10 + 0 (NaN skipped) + 20*5 = 1100.
    EXPECT_NEAR(agg.grossNotional(), 1100.0, 1e-9);
    agg.close();
}

TEST(breachKillsEveryGateAndTripsOnce) {
    HftRiskGate g0(2), g1(2);
    g0.onFill(0, Side::BUY, 100);
    g0.setReferencePrice(0, 100.0);   // gross = 10000

    GlobalRiskAggregator agg({&g0, &g1}, 5000.0, 0.5, POLL_NANOS);
    EXPECT_TRUE(waitFor([&] { return agg.isTripped(); }));
    EXPECT_TRUE(g0.isKilled());
    EXPECT_TRUE(g1.isKilled());   // gate-wide: even the gate with zero exposure
    EXPECT_TRUE(agg.tripCount() == 1);
    agg.close();
}

TEST(recoveryIsHystereticNotJustBelowCap) {
    HftRiskGate g0(2);
    g0.onFill(0, Side::BUY, 100);
    g0.setReferencePrice(0, 100.0);   // gross = 10000, cap = 5000

    GlobalRiskAggregator agg({&g0}, 5000.0, 0.5, POLL_NANOS);   // resume at 2500
    EXPECT_TRUE(waitFor([&] { return agg.isTripped(); }));

    // Reduce to 4000: below cap but ABOVE the 2500 resume threshold ->
    // must stay tripped.
    g0.onFill(0, Side::SELL, 60);   // position 40 * 100 = 4000
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    EXPECT_TRUE(agg.isTripped());
    EXPECT_TRUE(g0.isKilled());

    // Reduce further to 2000: below the resume threshold -> recovers.
    g0.onFill(0, Side::SELL, 20);    // position 20 * 100 = 2000
    EXPECT_TRUE(waitFor([&] { return !agg.isTripped(); }));
    EXPECT_TRUE(!g0.isKilled());
    agg.close();
}

TEST(killedBeforeTripStaysKilledAfterRecovery) {
    HftRiskGate g0(2), g1(2);
    g0.onFill(0, Side::BUY, 100);
    g0.setReferencePrice(0, 100.0);   // gross = 10000, cap = 5000

    // Ops already killed g1 BEFORE the aggregator ever trips.
    g1.kill(true);

    GlobalRiskAggregator agg({&g0, &g1}, 5000.0, 0.5, POLL_NANOS);
    EXPECT_TRUE(waitFor([&] { return agg.isTripped(); }));
    EXPECT_TRUE(g0.isKilled());
    EXPECT_TRUE(g1.isKilled());

    // Bring gross back under the resume threshold.
    g0.onFill(0, Side::SELL, 80);   // position 20 * 100 = 2000 < 2500 resume
    EXPECT_TRUE(waitFor([&] { return !agg.isTripped(); }));
    // The aggregator's own kill is lifted...
    EXPECT_TRUE(!g0.isKilled());
    // ...but g1's PRE-EXISTING kill (ops hold) must survive recovery.
    EXPECT_TRUE(g1.isKilled());
    agg.close();
}

TEST(constructorValidatesArguments) {
    HftRiskGate g0(2);
    EXPECT_THROWS(GlobalRiskAggregator({}, 1000.0, 0.9, POLL_NANOS));
    EXPECT_THROWS(GlobalRiskAggregator({&g0}, 0.0, 0.9, POLL_NANOS));
    EXPECT_THROWS(GlobalRiskAggregator({&g0}, 1000.0, 0.0, POLL_NANOS));
    EXPECT_THROWS(GlobalRiskAggregator({&g0}, 1000.0, 1.0, POLL_NANOS));
    EXPECT_THROWS(GlobalRiskAggregator({&g0}, 1000.0, 0.9, 0));
}

QTEST_MAIN()
