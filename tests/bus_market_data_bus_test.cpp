// Port of the Java HftMarketDataBus behavior pins: registerSymbol capacity
// enforcement, per-symbol and global listener dispatch (both fire, in
// registration order), latestPrice before/after the first tick, and a real
// producer-thread + consumer-thread SPSC run where the bus is driven
// through start()/stop() -- spin-until-published so processedCount() must
// equal SENT exactly (conservation under backpressure, the C++ mirror of
// the Java ring's acquire/release discipline).
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::HftMarketDataBus;

TEST(registerSymbolBeyondCapacityThrows) {
    HftMarketDataBus bus(16, 2, false);
    bus.registerSymbol("A");
    bus.registerSymbol("B");
    EXPECT_THROWS(bus.registerSymbol("C"));
}

TEST(perSymbolAndGlobalListenersBothFireInOrder) {
    HftMarketDataBus bus(16, 4, false);
    int eur = bus.registerSymbol("EURUSD");
    int usd = bus.registerSymbol("USDJPY");

    std::vector<std::string> calls;
    bus.subscribe(eur, [&](int, double price, double, std::int64_t) {
        calls.push_back("sym1:" + std::to_string(price));
    });
    bus.subscribe(eur, [&](int, double price, double, std::int64_t) {
        calls.push_back("sym2:" + std::to_string(price));
    });
    bus.subscribeAll([&](int symbolId, double, double, std::int64_t) {
        calls.push_back("all:" + std::to_string(symbolId));
    });

    // Drive the ring directly (no consumer thread) via a manual drain --
    // publish, then start()/stop() to run the consumer loop briefly.
    bus.publish(eur, 1.10, 1e6, 1);
    bus.publish(usd, 150.0, 1e6, 2);
    bus.start();
    // Let the consumer drain (parked 200ns idle; give it real wall time).
    for (int i = 0; i < 1000 && bus.processedCount() < 2; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bus.stop();

    EXPECT_TRUE(bus.processedCount() == 2);
    // EURUSD tick: both symbol listeners then the global listener.
    EXPECT_TRUE(calls.size() == 4);
    EXPECT_TRUE(calls[0] == "sym1:1.100000");
    EXPECT_TRUE(calls[1] == "sym2:1.100000");
    EXPECT_TRUE(calls[2] == "all:" + std::to_string(eur));
    // USDJPY tick: no per-symbol listener, only the global one.
    EXPECT_TRUE(calls[3] == "all:" + std::to_string(usd));
}

TEST(latestPriceIsNanBeforeFirstTickThenTracksLastPublished) {
    HftMarketDataBus bus(16, 2, false);
    int id = bus.registerSymbol("X");
    EXPECT_TRUE(std::isnan(bus.latestPrice(id)));
    bus.publish(id, 42.0, 1, 1);
    bus.start();
    for (int i = 0; i < 1000 && bus.processedCount() < 1; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bus.stop();
    EXPECT_NEAR(bus.latestPrice(id), 42.0, 0.0);
    EXPECT_NEAR(bus.latestPrice("X"), 42.0, 0.0);
}

TEST(ringFullCountsFailedPublishAttempts) {
    // Tiny ring (rounds up to 4), never drained (no start()): the 5th
    // publish must fail and count exactly one ringFull.
    HftMarketDataBus bus(1, 1, false);
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(bus.publish(0, 1.0, 1.0, i));
    }
    EXPECT_TRUE(!bus.publish(0, 1.0, 1.0, 99));
    EXPECT_TRUE(bus.ringFullCount() == 1);
}

TEST(producerConsumerConservesEveryTickUnderSpinBackpressure) {
    // Real two-thread run driven through start()/publish()/stop(): the
    // producer spins until accepted (never drops), so processedCount()
    // must equal SENT exactly and the price checksum the listener
    // accumulates must match the producer-side checksum.
    HftMarketDataBus bus(128, 4, false);
    int id = bus.registerSymbol("SPIN");
    constexpr int SENT = 200000;

    std::atomic<std::int64_t> delivered{0};
    std::atomic<double> deliveredSum{0.0};
    bus.subscribeAll([&](int, double price, double, std::int64_t) {
        delivered.fetch_add(1, std::memory_order_relaxed);
        // Single consumer thread only touches this; relaxed store is fine.
        deliveredSum.store(deliveredSum.load(std::memory_order_relaxed) + price,
                           std::memory_order_relaxed);
    });

    bus.start();
    double publishedSum = 0;
    for (int i = 1; i <= SENT; i++) {
        double price = 100.0 + (i % 1000) * 0.01;
        while (!bus.publish(id, price, 1.0, i)) {
            // spin: the consumer is momentarily behind
        }
        publishedSum += price;
    }
    for (int i = 0; i < 20000 && bus.processedCount() < SENT; i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    bus.stop();

    EXPECT_TRUE(bus.processedCount() == SENT);
    EXPECT_TRUE(delivered.load() == SENT);
    EXPECT_NEAR(deliveredSum.load(), publishedSum, 1e-6);
}

QTEST_MAIN()
