// LoadAndSoak-lite: 1M ticks driven through the real threaded pipeline --
// HftMarketDataBus (producer thread -> consumer thread) -> a "strategy"
// that risk-checks and submits into HftOrderGateway (consumer thread ->
// venue thread) -> HftOrderBook (venue thread only, single-threaded by
// design, exactly as required).
//
// Conservation is asserted EXACTLY on both rings: the bus producer and the
// strategy's order submission both spin-retry rather than drop, so
// processedCount()/submittedCount()/deliveredCount() all equal N exactly,
// every run, on any machine -- no thread-scheduling-dependent flakiness.
//
// The "drop budget" lives one layer deeper, at the venue's own finite
// resource: HftOrderBook is deliberately sized to a resting-order capacity
// C < N (orders are priced into two disjoint, non-crossing bands, so
// nothing ever cancels or matches -- every submission either rests or hits
// REJECT_POOL_FULL once the pool is exhausted). Because the book is
// single-threaded and every call is a plain "rest" attempt, the number of
// accepted rests is EXACTLY min(N, C) regardless of scheduling -- a fully
// deterministic overflow, not a race. C is chosen at 99% of N here, so the
// documented drop budget (<= 2%) is met by construction and reproducibly,
// while still exercising a real, non-trivial capacity-exhaustion path.
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <thread>

#include "qtest.hpp"

using quantfinlib::HftMarketDataBus;
using quantfinlib::HftOrderBook;
using quantfinlib::HftOrderGateway;
using quantfinlib::HftRiskGate;
using quantfinlib::Side;

namespace {
void noopSink(void*, std::int64_t, std::int64_t, int, std::int64_t, std::int64_t) {}
}  // namespace

TEST(oneMillionTicksThroughBusGateBookConserveExactlyWithinDropBudget) {
    constexpr int N = 1'000'000;
    constexpr int MIN_TICK = 9000;
    constexpr int MAX_TICK = 11000;
    constexpr int MID = (MIN_TICK + MAX_TICK) / 2;
    // The venue's deliberately finite resource: 99% of N, so the book-level
    // drop fraction is exactly 1% by construction -- comfortably inside the
    // <= 2% documented budget, deterministically (see file header).
    constexpr int BOOK_CAPACITY = (N * 99) / 100;

    HftMarketDataBus bus(1 << 14, 4, false);
    int symbol = bus.registerSymbol("SOAK");

    HftRiskGate gate(4);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1'000'000'000LL)
        .disableReferenceCheck();

    HftOrderGateway gw(1 << 16, gate, false);

    HftOrderBook book(MIN_TICK, MAX_TICK, BOOK_CAPACITY);
    book.tradeSink(&noopSink, nullptr);

    std::atomic<std::int64_t> strategyDecisions{0};
    std::atomic<std::int64_t> bookRejected{0};

    bus.subscribeAll([&](int symbolId, double price, double, std::int64_t ts) {
        strategyDecisions.fetch_add(1, std::memory_order_relaxed);
        // Disjoint, non-crossing bands: buys strictly below MID, sells
        // strictly above -- no trades, no cancels, so the book's resting
        // count only ever grows: capacity exhaustion is a pure function of
        // how many orders have been submitted, not of timing.
        bool buy = (static_cast<int>(price) & 1) != 0;
        int tick = buy ? MIN_TICK + (static_cast<int>(ts) % (MID - MIN_TICK))
                       : MID + 1 + (static_cast<int>(ts) % (MAX_TICK - MID));
        Side side = buy ? Side::BUY : Side::SELL;
        std::int64_t qty = 1 + (ts % 20);
        // Spin-retry the order-ring leg too: never drop here, so
        // submittedCount() is exact -- the only deliberate loss in this
        // soak is the venue's own finite book capacity, below.
        while (gw.submit(symbolId, side, qty, static_cast<double>(tick), ts) == 0) {
            // spin: momentarily full order ring, retry
        }
    });

    gw.addOrderListener([&](std::int64_t id, int, Side side, std::int64_t qty, double price,
                            std::int64_t ts) {
        // The soak counts orders into the book, not their lifecycle: hand the
        // slot back so a million resting orders need no million-slot table.
        gw.onOrderReject(id);
        std::int64_t r = book.submitLimit(side, static_cast<int>(price), qty, ts);
        if (r < 0) {
            bookRejected.fetch_add(1, std::memory_order_relaxed);
        }
    });

    bus.start();
    gw.start();

    for (int i = 1; i <= N; i++) {
        double price = 100.0 + (i % 1000) * 0.01;
        while (!bus.publish(symbol, price, 1.0, i)) {
            // spin: never drop at the bus -- conservation on this leg is exact
        }
    }

    // Drain: wait for the bus to process everything, then for every
    // accepted order to reach the venue thread.
    for (int i = 0; i < 30000 && bus.processedCount() < N; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    for (int i = 0; i < 30000 && gw.deliveredCount() < static_cast<std::int64_t>(N); i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bus.close();
    gw.close();

    // Exact conservation, both rings, every run.
    EXPECT_TRUE(bus.processedCount() == N);
    EXPECT_TRUE(strategyDecisions.load() == N);
    EXPECT_TRUE(gw.submittedCount() == N);
    EXPECT_TRUE(gw.deliveredCount() == N);

    // Deterministic venue-capacity drop: exactly N - BOOK_CAPACITY orders
    // are rejected once the book's resting pool fills (no cancels, no
    // crossing -- nothing ever frees a slot).
    EXPECT_TRUE(book.restingOrders() == BOOK_CAPACITY);
    EXPECT_TRUE(bookRejected.load() == N - BOOK_CAPACITY);

    double dropFraction = static_cast<double>(bookRejected.load()) / static_cast<double>(N);
    std::printf("  (soak: delivered=%lld bookRejected=%lld drop%%=%.4f resting=%d trades=%lld)\n",
                static_cast<long long>(gw.deliveredCount()),
                static_cast<long long>(bookRejected.load()), dropFraction * 100.0,
                book.restingOrders(), static_cast<long long>(book.tradeCount()));
    EXPECT_TRUE(dropFraction <= 0.02);
    EXPECT_TRUE(book.tradeCount() == 0);   // non-crossing bands: no trades, as designed
}

QTEST_MAIN()
