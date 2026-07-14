// Port of the Java HftQuoter behavior pins: skew/bid/ask arithmetic and
// grid snapping, the conflation rule (BOTH the move gate AND the interval
// gate must pass to suppress), the withConflation(0, ...) pitfall (a zero
// interval disables conflation entirely -- nothing is ever "younger than
// 0 ns"), withMinMove's purely move-gated conflation, and the mixed-book
// note that per-symbol config (a EURUSD half-spread is ~100x too tight for
// USDJPY) requires configureSymbol, not one constructor default.
//
// Test-harness note: the order listener runs on the gateway's venue
// thread, so the wait condition below is always an atomic counter (never
// vector::size() read racily from the test thread); the vector itself is
// only inspected AFTER gw.close(), which joins the venue thread and makes
// every prior push_back visible without a data race.
#include "quantfinlib/trading/hft_quoter.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::HftOrderGateway;
using quantfinlib::HftQuoter;
using quantfinlib::HftRiskGate;
using quantfinlib::Side;
using quantfinlib::TickSizeSchedule;

namespace {

struct Fill {
    Side side;
    double price;
};

template <typename Pred>
bool waitFor(Pred pred, int maxMs = 2000) {
    for (int i = 0; i < maxMs; i++) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return pred();
}

}  // namespace

TEST(configValidatesArguments) {
    EXPECT_THROWS(HftQuoter::Config::of(0, 0.01));
    EXPECT_THROWS(HftQuoter::Config::of(100, 0));
    EXPECT_THROWS(HftQuoter::Config::of(100, 0.01).withSkewPerUnit(-1));
}

TEST(basicTickProducesTwoSidedQuoteAroundMidWithNoSkew) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    std::vector<Fill> orders;
    std::atomic<int> count{0};
    gw.addOrderListener([&](std::int64_t, int, Side side, std::int64_t, double price,
                            std::int64_t) {
        orders.push_back({side, price});
        count.fetch_add(1, std::memory_order_release);
    });
    gw.start();

    HftQuoter quoter(gw, 2, HftQuoter::Config::of(100, 0.01));
    quoter.onTick(0, 1.1000, 1e6, 1000);

    EXPECT_TRUE(waitFor([&] { return count.load(std::memory_order_acquire) == 2; }));
    gw.close();

    EXPECT_TRUE(quoter.quoteUpdates() == 1);
    EXPECT_TRUE(quoter.rejectedSides() == 0);
    EXPECT_TRUE(orders.size() == 2);
    EXPECT_TRUE(orders[0].side == Side::BUY);
    EXPECT_NEAR(orders[0].price, 1.0900, 1e-9);
    EXPECT_TRUE(orders[1].side == Side::SELL);
    EXPECT_NEAR(orders[1].price, 1.1100, 1e-9);
}

TEST(longInventoryShadesBothSidesDown) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    gw.start();
    // Book a long position directly on the gate (as a fill would).
    gate.onFill(0, Side::BUY, 1000);

    std::vector<Fill> orders;
    std::atomic<int> count{0};
    gw.addOrderListener([&](std::int64_t, int, Side side, std::int64_t, double price,
                            std::int64_t) {
        orders.push_back({side, price});
        count.fetch_add(1, std::memory_order_release);
    });

    HftQuoter quoter(gw, 2, HftQuoter::Config::of(100, 0.01).withSkewPerUnit(0.0001));
    quoter.onTick(0, 1.1000, 1e6, 1000);
    EXPECT_TRUE(waitFor([&] { return count.load(std::memory_order_acquire) == 2; }));
    gw.close();

    EXPECT_TRUE(orders.size() == 2);
    // skew = -1000 * 0.0001 = -0.1 -> both sides shift down by 0.1.
    EXPECT_NEAR(orders[0].price, 1.0900 - 0.1, 1e-9);
    EXPECT_NEAR(orders[1].price, 1.1100 - 0.1, 1e-9);
}

TEST(conflationSuppressesOnlyWhenBothMoveAndIntervalGatesPass) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    gw.start();

    HftQuoter quoter(gw, 2,
                     HftQuoter::Config::of(100, 0.01).withConflation(1000, 0.01));
    quoter.onTick(0, 1.1000, 1e6, 0);
    EXPECT_TRUE(quoter.quoteUpdates() == 1);

    // Small move (< 0.01) AND still within the 1000ns window -> suppressed.
    quoter.onTick(0, 1.1005, 1e6, 500);
    EXPECT_TRUE(quoter.quoteUpdates() == 1);
    EXPECT_TRUE(quoter.suppressedUpdates() == 1);

    // Big move (>= 0.01) even though still within the window -> NOT suppressed
    // (only one gate would pass).
    quoter.onTick(0, 1.2000, 1e6, 600);
    EXPECT_TRUE(quoter.quoteUpdates() == 2);

    // Small move but the window has elapsed -> NOT suppressed either.
    quoter.onTick(0, 1.2005, 1e6, 600 + 2000);
    EXPECT_TRUE(quoter.quoteUpdates() == 3);
    gw.close();
}

TEST(withConflationZeroIntervalDisablesConflationEntirely) {
    // Pitfall pin: minIntervalNanos = 0 means nothing is ever "younger
    // than 0 ns", so EVERY tick requotes regardless of how small the move
    // is -- withConflation(0, x) does not mean "suppress on move alone".
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    gw.start();

    HftQuoter quoter(gw, 2, HftQuoter::Config::of(100, 0.01).withConflation(0, 0.01));
    quoter.onTick(0, 1.1000, 1e6, 0);
    quoter.onTick(0, 1.10001, 1e6, 1);   // move far below 0.01, same instant+1ns
    quoter.onTick(0, 1.10002, 1e6, 2);
    gw.close();

    EXPECT_TRUE(quoter.quoteUpdates() == 3);
    EXPECT_TRUE(quoter.suppressedUpdates() == 0);
}

TEST(withMinMoveIsPurelyMoveGatedRegardlessOfAge) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    gw.start();

    HftQuoter quoter(gw, 2, HftQuoter::Config::of(100, 0.01).withMinMove(0.01));
    quoter.onTick(0, 1.1000, 1e6, 0);
    EXPECT_TRUE(quoter.quoteUpdates() == 1);

    // A huge time gap does not matter: the move is what counts.
    quoter.onTick(0, 1.1005, 1e6, 1'000'000'000'000LL);
    EXPECT_TRUE(quoter.quoteUpdates() == 1);
    EXPECT_TRUE(quoter.suppressedUpdates() == 1);

    quoter.onTick(0, 1.1105, 1e6, 2'000'000'000'000LL);
    EXPECT_TRUE(quoter.quoteUpdates() == 2);
    gw.close();
}

TEST(mixedBookUsesPerSymbolConfigViaConfigureSymbol) {
    // EURUSD-vs-USDJPY scale note: a 1-pip EURUSD half-spread (~0.0001) is
    // ~100x too tight for USDJPY (~0.01 pip-equivalent); one quoter serves
    // both only because configureSymbol overrides the per-symbol default.
    HftRiskGate gate(2);
    // Cap comfortably above the 1,000,000-unit quote size used below.
    gate.maxOrderQuantity(2000000).maxOrderNotional(1e9).maxPositionQuantity(10000000);
    HftOrderGateway gw(gate);
    gw.start();

    std::vector<Fill> orders;
    std::atomic<int> count{0};
    gw.addOrderListener([&](std::int64_t, int symbolId, Side side, std::int64_t, double price,
                            std::int64_t) {
        (void)symbolId;
        orders.push_back({side, price});
        count.fetch_add(1, std::memory_order_release);
    });

    HftQuoter quoter(gw, 2, HftQuoter::Config::of(1000000, 0.0001));   // EURUSD-scale default
    quoter.configureSymbol(1, HftQuoter::Config::of(1000000, 0.01));  // USDJPY-scale override

    quoter.onTick(0, 1.1000, 1e6, 0);     // symbol 0: EURUSD default
    quoter.onTick(1, 150.00, 1e6, 0);     // symbol 1: USDJPY override
    EXPECT_TRUE(waitFor([&] { return count.load(std::memory_order_acquire) == 4; }));
    gw.close();

    EXPECT_TRUE(quoter.rejectedSides() == 0);
    EXPECT_TRUE(orders.size() == 4);
    EXPECT_NEAR(orders[0].price, 1.1000 - 0.0001, 1e-9);
    EXPECT_NEAR(orders[1].price, 1.1000 + 0.0001, 1e-9);
    EXPECT_NEAR(orders[2].price, 150.00 - 0.01, 1e-9);
    EXPECT_NEAR(orders[3].price, 150.00 + 0.01, 1e-9);
}

TEST(gridSnapRoundsBidDownAndAskUpTowardPassivity) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000);
    HftOrderGateway gw(gate);
    gw.start();

    std::vector<Fill> orders;
    std::atomic<int> count{0};
    gw.addOrderListener([&](std::int64_t, int, Side side, std::int64_t, double price,
                            std::int64_t) {
        orders.push_back({side, price});
        count.fetch_add(1, std::memory_order_release);
    });

    auto schedule = std::make_shared<TickSizeSchedule>(TickSizeSchedule::flat(0.01));
    HftQuoter quoter(gw, 2,
                     HftQuoter::Config::of(100, 0.015).withTickSchedule(schedule));
    quoter.onTick(0, 100.00, 1e6, 0);   // bid=99.985 ask=100.015 -> snap to 0.01 grid
    EXPECT_TRUE(waitFor([&] { return count.load(std::memory_order_acquire) == 2; }));
    gw.close();

    EXPECT_TRUE(orders.size() == 2);
    EXPECT_NEAR(orders[0].price, 99.98, 1e-9);    // round DOWN
    EXPECT_NEAR(orders[1].price, 100.02, 1e-9);   // round UP
}

QTEST_MAIN()
