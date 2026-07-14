// Port of the SPSC ring behavior pins (Java OrderRingBuffer /
// TickRingBuffer): capacity rounding, in-order zero-loss delivery, the
// full/backpressure contract, and a real two-thread producer/consumer
// conservation run — delivered + dropped == sent, and the delivered
// payload checksum matches exactly what the producer published (the
// acquire/release publication protocol proven, not assumed).
#include <atomic>
#include <cstdint>
#include <thread>

#include "quantfinlib/trading/order_ring_buffer.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"
#include "qtest.hpp"

using quantfinlib::OrderRingBuffer;
using quantfinlib::Side;
using quantfinlib::TickRingBuffer;

TEST(capacityRoundsUpToPowersOfTwo) {
    // Java: Integer.highestOneBit(Math.max(2, requested - 1)) * 2.
    EXPECT_TRUE(OrderRingBuffer(1).capacity() == 4);
    EXPECT_TRUE(OrderRingBuffer(4).capacity() == 4);
    EXPECT_TRUE(OrderRingBuffer(5).capacity() == 8);
    EXPECT_TRUE(OrderRingBuffer(8).capacity() == 8);
    EXPECT_TRUE(OrderRingBuffer(1000).capacity() == 1024);
    EXPECT_TRUE(TickRingBuffer(1024).capacity() == 1024);
    EXPECT_TRUE(TickRingBuffer(1025).capacity() == 2048);
}

TEST(ordersFlowInOrderAndFullRingRefusesPublish) {
    OrderRingBuffer ring(4);
    EXPECT_TRUE(ring.isEmpty());
    for (int i = 0; i < 4; i++) {
        EXPECT_TRUE(ring.publish(100 + i, i, i % 2 == 0 ? Side::BUY : Side::SELL,
                                 10 + i, 99.5 + i, 1000 + i));
    }
    // Full: the producer chooses the backpressure policy.
    EXPECT_TRUE(!ring.publish(999, 0, Side::BUY, 1, 1.0, 1));
    EXPECT_TRUE(!ring.isEmpty());

    int seen = 0;
    int n = ring.drainTo(
            [&seen](std::int64_t orderId, int symbolId, Side side, std::int64_t qty,
                    double price, std::int64_t ts) {
                EXPECT_TRUE(orderId == 100 + seen);  // strict FIFO
                EXPECT_TRUE(symbolId == seen);
                EXPECT_TRUE(side == (seen % 2 == 0 ? Side::BUY : Side::SELL));
                EXPECT_TRUE(qty == 10 + seen);
                EXPECT_NEAR(price, 99.5 + seen, 0.0);
                EXPECT_TRUE(ts == 1000 + seen);
                seen++;
            },
            16);
    EXPECT_TRUE(n == 4);
    EXPECT_TRUE(seen == 4);
    EXPECT_TRUE(ring.isEmpty());
    // Freed capacity is reusable (wrap-around).
    EXPECT_TRUE(ring.publish(200, 0, Side::BUY, 1, 1.0, 1));
    EXPECT_TRUE(ring.drainTo([](std::int64_t, int, Side, std::int64_t, double,
                                std::int64_t) {},
                             16) == 1);
}

TEST(drainToHonorsTheLimit) {
    TickRingBuffer ring(16);
    for (int i = 0; i < 10; i++) {
        EXPECT_TRUE(ring.publish(i, 100.0 + i, 1.0, i));
    }
    int count = 0;
    EXPECT_TRUE(ring.drainTo([&count](int, double, double, std::int64_t) { count++; }, 3)
                == 3);
    EXPECT_TRUE(count == 3);
    // The remaining 7 arrive next, in order, starting where we stopped.
    int first = -1;
    EXPECT_TRUE(ring.drainTo(
                        [&first](int symbolId, double, double, std::int64_t) {
                            if (first < 0) {
                                first = symbolId;
                            }
                        },
                        100)
                == 7);
    EXPECT_TRUE(first == 3);
    EXPECT_TRUE(ring.isEmpty());
}

TEST(spscProducerConsumerConservesEveryOrder) {
    // Real two-thread run under the drop-when-full policy: the producer
    // counts drops, the consumer counts deliveries, and at the end
    // delivered + dropped == sent AND the delivered quantity checksum
    // equals the producer-side checksum of exactly the published orders --
    // nothing lost, nothing duplicated, nothing torn.
    OrderRingBuffer ring(256);
    constexpr int SENT = 500000;

    std::int64_t publishedSum = 0;   // producer-side checksum of accepted orders
    std::int64_t dropped = 0;
    std::atomic<bool> done{false};

    std::thread producer([&] {
        for (int i = 1; i <= SENT; i++) {
            std::int64_t qty = 1 + (i % 97);
            if (ring.publish(i, i % 64, (i & 1) ? Side::BUY : Side::SELL, qty,
                             100.0 + (i % 50), i)) {
                publishedSum += qty;
            } else {
                dropped++;
            }
        }
        done.store(true, std::memory_order_release);
    });

    std::int64_t delivered = 0;
    std::int64_t deliveredSum = 0;
    std::int64_t lastId = 0;
    bool ordered = true;
    auto sink = [&](std::int64_t orderId, int, Side, std::int64_t qty, double,
                    std::int64_t) {
        ordered = ordered && orderId > lastId;  // ids strictly increase
        lastId = orderId;
        delivered++;
        deliveredSum += qty;
    };
    while (!done.load(std::memory_order_acquire)) {
        ring.drainTo(sink, 64);
    }
    while (ring.drainTo(sink, 64) > 0) {
    }
    producer.join();

    EXPECT_TRUE(delivered + dropped == SENT);
    EXPECT_TRUE(deliveredSum == publishedSum);
    EXPECT_TRUE(ordered);
    EXPECT_TRUE(ring.isEmpty());
}

TEST(spscTickRingConservesUnderSpinBackpressure) {
    // Spin-until-accepted policy: nothing may be dropped, so the consumer
    // must see every tick exactly once (delivered == sent) with the price
    // checksum intact.
    TickRingBuffer ring(128);
    constexpr int SENT = 200000;
    std::atomic<bool> done{false};
    double publishedSum = 0;

    std::thread producer([&] {
        for (int i = 1; i <= SENT; i++) {
            double price = 100.0 + (i % 1000) * 0.01;
            while (!ring.publish(i % 32, price, 1.0, i)) {
                // spin: the consumer is momentarily behind
            }
            publishedSum += price;
        }
        done.store(true, std::memory_order_release);
    });

    std::int64_t delivered = 0;
    double deliveredSum = 0;
    auto sink = [&](int, double price, double, std::int64_t) {
        delivered++;
        deliveredSum += price;
    };
    while (!done.load(std::memory_order_acquire)) {
        ring.drainTo(sink, 64);
    }
    while (ring.drainTo(sink, 64) > 0) {
    }
    producer.join();

    EXPECT_TRUE(delivered == SENT);
    EXPECT_NEAR(deliveredSum, publishedSum, 1e-6);
}

QTEST_MAIN()
