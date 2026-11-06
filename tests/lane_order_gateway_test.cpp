// Port of the Java HftOrderGateway behavior pins: the submit() return-code
// convention (positive = accepted id, negative = -riskGate reason code,
// zero = ring full), the venue-thread batch-of-256 delivered counter, and
// a two-thread producer/consumer conservation run through start()/stop().
#include "quantfinlib/trading/hft_order_gateway.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::HftOrderGateway;
using quantfinlib::HftRiskGate;
using quantfinlib::Side;

TEST(submitReturnsPositiveIdOnAcceptNegativeReasonOnRejectZeroOnRingFull) {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(1000)
        .disableReferenceCheck();
    HftOrderGateway gw(4, gate, false);   // rounds to 4-slot ring, no consumer started

    std::int64_t id1 = gw.submit(0, Side::BUY, 10, 100.0, 1);
    EXPECT_TRUE(id1 == 1);
    std::int64_t id2 = gw.submit(0, Side::BUY, 10, 100.0, 2);
    EXPECT_TRUE(id2 == 2);

    // Over the quantity cap: rejected with -REJECT_QUANTITY.
    std::int64_t rejected = gw.submit(0, Side::BUY, 1000, 100.0, 3);
    EXPECT_TRUE(rejected == -HftRiskGate::REJECT_QUANTITY);

    // Fill the 4-slot ring (2 more accepted orders), then the next accepted
    // risk check still hits a full ring -> 0.
    gw.submit(0, Side::BUY, 10, 100.0, 4);
    gw.submit(0, Side::BUY, 10, 100.0, 5);
    std::int64_t full = gw.submit(0, Side::BUY, 10, 100.0, 6);
    EXPECT_TRUE(full == 0);
    EXPECT_TRUE(gw.ringFullCount() == 1);
    EXPECT_TRUE(gw.submittedCount() == 4);
}

TEST(venueThreadDeliversEveryAcceptedOrder) {
    HftRiskGate gate(2);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .disableReferenceCheck();
    HftOrderGateway gw(gate);

    std::atomic<int> received{0};
    gw.addOrderListener([&](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {
        received.fetch_add(1, std::memory_order_relaxed);
    });
    gw.start();
    constexpr int N = 5000;
    for (int i = 0; i < N; i++) {
        gw.submit(0, Side::BUY, 1, 100.0, i);
    }
    for (int i = 0; i < 5000 && gw.deliveredCount() < N; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    gw.stop();
    EXPECT_TRUE(gw.deliveredCount() == N);
    EXPECT_TRUE(received.load() == N);
}

TEST(twoThreadProducerConsumerConservesEveryOrderUnderSpin) {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(100000000)
        .disableReferenceCheck();
    HftOrderGateway gw(64, gate, false);

    std::atomic<std::int64_t> delivered{0};
    std::atomic<std::int64_t> deliveredQtySum{0};
    // The venue fills each order at once, so reservations and table slots
    // come back and the producer never meets the position cap.
    gw.addOrderListener([&](std::int64_t id, int symbolId, Side side, std::int64_t qty, double,
                            std::int64_t) {
        delivered.fetch_add(1, std::memory_order_relaxed);
        deliveredQtySum.fetch_add(qty, std::memory_order_relaxed);
        gw.onFill(id, symbolId, side, qty);
    });
    gw.start();

    constexpr int SENT = 100000;
    std::int64_t sentQtySum = 0;
    std::atomic<int> accepted{0};
    std::thread producer([&] {
        for (int i = 0; i < SENT; i++) {
            std::int64_t qty = 1 + (i % 50);
            std::int64_t r;
            do {
                r = gw.submit(i % 4, Side::BUY, qty, 100.0, i);
            } while (r == 0);  // spin past a full ring; never drop
            accepted.fetch_add(1, std::memory_order_relaxed);
            sentQtySum += qty;
        }
    });
    producer.join();

    for (int i = 0; i < 20000 && gw.deliveredCount() < static_cast<std::int64_t>(SENT); i++) {
        std::this_thread::sleep_for(std::chrono::microseconds(200));
    }
    gw.stop();

    EXPECT_TRUE(accepted.load() == SENT);
    EXPECT_TRUE(gw.submittedCount() == SENT);
    EXPECT_TRUE(gw.deliveredCount() == SENT);
    EXPECT_TRUE(delivered.load() == SENT);
    EXPECT_TRUE(deliveredQtySum.load() == sentQtySum);
    EXPECT_TRUE(gate.position(0) + gate.position(1) + gate.position(2) + gate.position(3)
                == sentQtySum);
    EXPECT_TRUE(gw.openOrderCount() == 0);
}

QTEST_MAIN()
