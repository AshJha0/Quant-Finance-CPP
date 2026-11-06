// Regression tests for the 2026-11-06 review findings on the live lane:
// H1 (listener load must be lock-free), H2 (symbolId bounds, listener
// exceptions), H3 (one cache line per ring message), H4 (bus counters read
// across threads; run this binary under TSan), plus the ring and hiccup
// monitor edge cases found alongside them.
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <thread>
#include <vector>

#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/util/hiccup_monitor.hpp"
#include "quantfinlib/util/latency_recorder.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"
#include "qtest.hpp"

using quantfinlib::HftMarketDataBus;
using quantfinlib::HiccupMonitor;
using quantfinlib::LatencyRecorder;
using quantfinlib::TickRingBuffer;

namespace {

template <typename Fn>
bool throwsInvalid(Fn fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

void waitProcessed(const HftMarketDataBus& bus, std::int64_t n) {
    while (bus.processedCount() < n) {
        std::this_thread::yield();
    }
}

}  // namespace

static_assert(HftMarketDataBus::listenerLoadIsLockFree());
static_assert(TickRingBuffer::SLOT_BYTES == 64);

TEST(publishRefusesOutOfRangeSymbolIds) {
    HftMarketDataBus bus(16, 2, false);
    int id = bus.registerSymbol("A");
    EXPECT_TRUE(!bus.publish(-1, 1.0, 1.0, 0));
    EXPECT_TRUE(!bus.publish(2, 1.0, 1.0, 0));
    EXPECT_TRUE(!bus.publish(1 << 30, 1.0, 1.0, 0));
    EXPECT_TRUE(bus.invalidSymbolCount() == 3);
    EXPECT_TRUE(bus.ringFullCount() == 0);
    EXPECT_TRUE(bus.publish(id, 1.0, 1.0, 0));
    bus.start();
    waitProcessed(bus, 1);
    bus.close();
    EXPECT_TRUE(bus.processedCount() == 1);   // refused ticks never entered the ring
}

TEST(coldPathRejectsBadIdsAndEmptyListeners) {
    HftMarketDataBus bus(16, 2, false);
    bool outOfRange = false;
    try {
        bus.subscribe(2, [](int, double, double, std::int64_t) {});
    } catch (const std::out_of_range&) {
        outOfRange = true;
    }
    EXPECT_TRUE(outOfRange);
    EXPECT_TRUE(throwsInvalid([&] { bus.subscribe(0, quantfinlib::TickListener{}); }));
    EXPECT_TRUE(throwsInvalid([&] { bus.subscribeAll(quantfinlib::TickListener{}); }));
    EXPECT_TRUE(std::isnan(bus.latestPrice(-1)));
    EXPECT_TRUE(std::isnan(bus.latestPrice(2)));
    bool badName = false;
    try {
        (void)bus.symbol(7);
    } catch (const std::out_of_range&) {
        badName = true;
    }
    EXPECT_TRUE(badName);
    EXPECT_TRUE(throwsInvalid([] { HftMarketDataBus b(16, 0, false); }));
    EXPECT_TRUE(throwsInvalid([] { HftMarketDataBus b(16, -1, false); }));
}

TEST(aThrowingListenerIsCountedAndDoesNotStopTheOthers) {
    HftMarketDataBus bus(64, 2, false);
    int id = bus.registerSymbol("A");
    std::atomic<int> after{0};
    std::atomic<int> global{0};
    bus.subscribe(id, [](int, double, double, std::int64_t) {
        throw std::runtime_error("strategy bug");
    });
    bus.subscribe(id, [&](int, double, double, std::int64_t) { after++; });
    bus.subscribeAll([&](int, double, double, std::int64_t) { global++; });
    bus.start();
    for (int i = 0; i < 10; i++) {
        while (!bus.publish(id, 100.0 + i, 1.0, i)) {
        }
    }
    waitProcessed(bus, 10);
    bus.close();
    EXPECT_TRUE(after == 10);
    EXPECT_TRUE(global == 10);
    EXPECT_TRUE(bus.listenerErrorCount() == 10);
    EXPECT_TRUE(bus.latestPrice(id) == 109.0);
}

// Retired lists must stay readable for a dispatch in flight: ASan and TSan
// both watch this one.
TEST(subscribingDuringTrafficIsSafeAndEveryListenerStaysLive) {
    HftMarketDataBus bus(1 << 10, 2, true);
    int id = bus.registerSymbol("A");
    std::atomic<std::int64_t> calls{0};
    bus.start();
    std::atomic<bool> done{false};
    std::thread producer([&] {
        std::int64_t i = 0;
        while (!done.load(std::memory_order_acquire)) {
            bus.publish(id, 1.0, 1.0, i++);
        }
    });
    for (int k = 0; k < 200; k++) {
        bus.subscribe(id, [&](int, double, double, std::int64_t) { calls++; });
        bus.subscribeAll([&](int, double, double, std::int64_t) { calls++; });
    }
    done.store(true, std::memory_order_release);
    producer.join();
    bus.close();

    std::int64_t before = calls.load();
    bus.start();
    while (!bus.publish(id, 1.0, 1.0, 0)) {
    }
    std::int64_t target = bus.processedCount() + 1;
    bus.close();   // drains; also frees retired lists while listeners stay owned
    EXPECT_TRUE(bus.processedCount() == target);
    EXPECT_TRUE(calls.load() - before == 400);
}

TEST(countersMayBeReadWhileTheLaneRuns) {
    HftMarketDataBus bus(4, 2, true);
    int id = bus.registerSymbol("A");
    bus.subscribe(id, [](int, double, double, std::int64_t) { throw 1; });
    bus.start();
    std::atomic<bool> stop{false};
    std::int64_t seen = 0;
    std::thread monitor([&] {
        while (!stop.load(std::memory_order_acquire)) {
            seen += bus.ringFullCount() + bus.invalidSymbolCount() + bus.listenerErrorCount();
        }
    });
    for (int i = 0; i < 100000; i++) {
        bus.publish(i % 1000 == 0 ? -1 : id, 1.0, 1.0, i);
    }
    stop.store(true, std::memory_order_release);
    monitor.join();
    bus.close();
    EXPECT_TRUE(bus.invalidSymbolCount() == 100);
    EXPECT_TRUE(seen >= 0);
}

TEST(startStopCyclesFromTwoThreadsNeverRunTwoConsumers) {
    HftMarketDataBus bus(64, 2, false);
    int id = bus.registerSymbol("A");
    std::int64_t plainCount = 0;   // TSan flags this if two consumers ever overlap
    bus.subscribe(id, [&](int, double, double, std::int64_t) { plainCount++; });
    std::atomic<bool> done{false};
    std::thread cycler([&] {
        while (!done.load(std::memory_order_acquire)) {
            bus.start();
            bus.stop();
        }
    });
    std::thread cycler2([&] {
        while (!done.load(std::memory_order_acquire)) {
            bus.start();
            bus.stop();
        }
    });
    int sent = 0;
    while (sent < 5000) {
        if (bus.publish(id, 1.0, 1.0, sent)) {
            sent++;
        } else {
            std::this_thread::yield();
        }
    }
    done.store(true, std::memory_order_release);
    cycler.join();
    cycler2.join();
    bus.start();
    waitProcessed(bus, sent);
    bus.close();
    EXPECT_TRUE(plainCount == sent);
}

TEST(ringKeepsTheApiWithOneSlotPerLine) {
    TickRingBuffer ring(8);
    EXPECT_TRUE(ring.capacity() == 8);
    for (int i = 0; i < 8; i++) {
        EXPECT_TRUE(ring.publish(i, i + 0.5, i + 0.25, 1000 + i));
    }
    EXPECT_TRUE(!ring.publish(9, 0, 0, 0));
    int next = 0;
    int got = ring.drainTo(
            [&](int s, double p, double z, std::int64_t t) {
                EXPECT_TRUE(s == next && p == next + 0.5 && z == next + 0.25
                            && t == 1000 + next);
                next++;
            },
            100);
    EXPECT_TRUE(got == 8);
}

TEST(ringRejectsImpossibleCapacityAndNonPositiveLimits) {
    EXPECT_TRUE(throwsInvalid([] { TickRingBuffer r(TickRingBuffer::MAX_CAPACITY + 1); }));
    TickRingBuffer ring(4);
    EXPECT_TRUE(ring.publish(1, 1.0, 1.0, 1));
    auto sink = [](int, double, double, std::int64_t) {};
    EXPECT_TRUE(ring.drainTo(sink, 0) == 0);
    EXPECT_TRUE(ring.drainTo(sink, -5) == 0);
    EXPECT_TRUE(!ring.isEmpty());
    EXPECT_TRUE(ring.drainTo(sink, 1) == 1);
}

TEST(percentileClampsItsArgument) {
    LatencyRecorder rec;
    rec.record(10);
    rec.record(20);
    EXPECT_TRUE(rec.percentile(std::nan("")) == 10);
    EXPECT_TRUE(rec.percentile(-1.0) == 10);
    EXPECT_TRUE(rec.percentile(2.0) == 20);
}

TEST(hiccupMonitorRestartsAndReadsWhileRunning) {
    HiccupMonitor m(100000);
    m.start();
    m.start();   // used to terminate the process: assignment over a joinable thread
    while (m.samples() < 3) {
        (void)m.maxHiccupNanos();
        (void)m.summary();
    }
    m.stop();
    m.start();   // same, after stop() without close()
    std::int64_t base = m.samples();
    while (m.samples() <= base) {
        std::this_thread::yield();
    }
    m.close();
    EXPECT_TRUE(m.recorder().count() > base);
}

QTEST_MAIN()
