// The C++ analogue of the Java allocation-counter tests
// (HftOrderBookTest.steadyStateOperationsAreAllocationFree and
// HftOrderBookTifTest.tifOperationsAreAllocationFree, which assert via
// ThreadMXBean.getThreadAllocatedBytes that the hot path allocates
// nothing). Here the whole test binary's global operator new/new[],
// including the aligned forms, is replaced with counting versions. After
// warm-up, one million mixed steady-state operations across the hot path
// (book limit/market/IOC/FOK/post-only/cancel, risk-gate check + onFill,
// order-ring and tick-ring publish + drain, latency-recorder record) and one
// million ticks through a running HftMarketDataBus must perform EXACTLY ZERO
// allocations -- stricter than Java's <100KB slack, because C++ has no JIT to
// appease. Direct malloc is not counted; no hot-path code calls it.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <malloc.h>
#endif

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"
#include "quantfinlib/util/latency_recorder.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"
#include "qtest.hpp"

// ------------------------------------------------------------------
// Counting global allocator: every route into the heap bumps the counter.
// ------------------------------------------------------------------

namespace {
std::atomic<long long> g_allocations{0};

void* countedAligned(std::size_t size, std::size_t align) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
#ifdef _WIN32
    return _aligned_malloc(size ? size : 1, align);
#else
    void* p = nullptr;
    return posix_memalign(&p, align < sizeof(void*) ? sizeof(void*) : align, size ? size : 1) == 0
            ? p
            : nullptr;
#endif
}

void alignedFree(void* p) noexcept {
#ifdef _WIN32
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}  // namespace

void* operator new(std::size_t size) {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    void* p = std::malloc(size ? size : 1);
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}

void* operator new[](std::size_t size) {
    return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(size ? size : 1);
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    return ::operator new(size, std::nothrow);
}

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

void* operator new(std::size_t size, std::align_val_t align) {
    void* p = countedAligned(size, static_cast<std::size_t>(align));
    if (!p) {
        throw std::bad_alloc();
    }
    return p;
}
void* operator new[](std::size_t size, std::align_val_t align) {
    return ::operator new(size, align);
}
void* operator new(std::size_t size, std::align_val_t align, const std::nothrow_t&) noexcept {
    return countedAligned(size, static_cast<std::size_t>(align));
}
void* operator new[](std::size_t size, std::align_val_t align,
                     const std::nothrow_t&) noexcept {
    return countedAligned(size, static_cast<std::size_t>(align));
}
void operator delete(void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { alignedFree(p); }
void operator delete(void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    alignedFree(p);
}
void operator delete[](void* p, std::align_val_t, const std::nothrow_t&) noexcept {
    alignedFree(p);
}

using quantfinlib::HftMarketDataBus;
using quantfinlib::HftOrderBook;
using quantfinlib::HftRiskGate;
using quantfinlib::OrderRingBuffer;
using quantfinlib::LatencyRecorder;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;
using quantfinlib::TickRingBuffer;

namespace {

constexpr int MIN_T = 900;
constexpr int MAX_T = 1100;

void blackholeSink(void*, std::int64_t, std::int64_t, int, std::int64_t, std::int64_t) {}

/**
 * One steady-state step across the whole hot path. Mirrors the Java churn
 * mixes: mostly adds (some crossing), some cancels from a preallocated id
 * ring, all four TIF entry points, one gate check + fill, one ring
 * publish + drain, one histogram record.
 */
int step(HftOrderBook& b, HftRiskGate& gate, OrderRingBuffer& ring, TickRingBuffer& ticks,
         LatencyRecorder& rec, SplittableRandom& rnd, std::int64_t* ids, int idCount,
         int idCap, int i) {
    Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
    int tick = MIN_T + rnd.nextInt(MAX_T - MIN_T + 1);
    std::int64_t qty = 1 + rnd.nextInt(50);

    if (gate.check(0, side, qty, tick / 100.0) == HftRiskGate::OK) {
        ring.publish(i, 0, side, qty, tick / 100.0, i);
    }
    ring.drainTo([](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {}, 8);

    ticks.publish(0, tick / 100.0, static_cast<double>(qty), i);
    ticks.drainTo([](int, double, double, std::int64_t) {}, 8);
    if ((i & 15) == 15) {
        b.submitMarket(side, qty, i);
    }

    if (rnd.nextInt(10) < 7 || idCount == 0) {
        std::int64_t id;
        switch (i & 3) {
            case 0: id = b.submitPostOnly(side, tick, qty, i); break;
            case 1: id = b.submitIoc(side, tick, qty, i); break;
            case 2: id = b.submitFok(side, tick, qty, i); break;
            default: id = b.submitLimit(side, tick, qty, i); break;
        }
        if ((i & 3) != 1 && (i & 3) != 2 && id > 0 && b.openQuantity(id) > 0
                && idCount < idCap) {
            ids[idCount++] = id;
        }
    } else {
        int pick = rnd.nextInt(idCount);
        b.cancel(ids[pick]);
        ids[pick] = ids[--idCount];  // swap-remove, no allocation
    }
    gate.onFill(0, side, 1);
    rec.record(qty);
    return idCount;
}

}  // namespace

TEST(oneMillionSteadyStateOpsAllocateNothing) {
    HftOrderBook book(MIN_T, MAX_T, 1 << 12);
    book.tradeSink(&blackholeSink, nullptr);
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(
            std::numeric_limits<std::int64_t>::max());
    OrderRingBuffer ring(1 << 10);
    TickRingBuffer ticks(1 << 10);
    LatencyRecorder rec;
    SplittableRandom rnd(42);

    constexpr int ID_CAP = 1024;
    static std::int64_t ids[ID_CAP];  // preallocated: the TEST must not allocate either
    int idCount = 0;

    // Warm-up: populate the book, fault every array page, exercise all paths.
    for (int i = 0; i < 200000; i++) {
        idCount = step(book, gate, ring, ticks, rec, rnd, ids, idCount, ID_CAP, i);
    }

    long long before = g_allocations.load(std::memory_order_relaxed);
    for (int i = 0; i < 1000000; i++) {
        idCount = step(book, gate, ring, ticks, rec, rnd, ids, idCount, ID_CAP, i);
    }
    long long allocated = g_allocations.load(std::memory_order_relaxed) - before;

    EXPECT_TRUE(allocated == 0);           // the claim that defines the hot lane
    EXPECT_TRUE(book.tradeCount() > 0);    // the loop exercised matching, not just resting
    EXPECT_TRUE(rec.count() == 1200000);
    // Sanity: the counter itself works (this vector allocation must register).
    long long probe = g_allocations.load(std::memory_order_relaxed);
    std::vector<int>* v = new std::vector<int>(64);
    delete v;
    EXPECT_TRUE(g_allocations.load(std::memory_order_relaxed) > probe);
}

TEST(oneMillionTicksThroughARunningBusAllocateNothing) {
    HftMarketDataBus bus(1 << 12, 4, true);
    int symbol = bus.registerSymbol("A");
    std::atomic<std::int64_t> perSymbol{0};
    std::atomic<std::int64_t> global{0};
    bus.subscribe(symbol, [&](int, double, double, std::int64_t) {
        perSymbol.fetch_add(1, std::memory_order_relaxed);
    });
    bus.subscribeAll([&](int, double, double, std::int64_t) {
        global.fetch_add(1, std::memory_order_relaxed);
    });
    bus.start();
    auto push = [&](int n) {
        for (int i = 0; i < n; i++) {
            while (!bus.publish(symbol, 100.0 + (i & 1), 1.0, i)) {
            }
        }
        while (global.load(std::memory_order_relaxed) < n) {
            std::this_thread::yield();
        }
    };
    push(20000);
    global.store(0, std::memory_order_relaxed);

    long long before = g_allocations.load(std::memory_order_relaxed);
    push(1000000);
    long long allocated = g_allocations.load(std::memory_order_relaxed) - before;
    bus.close();

    EXPECT_TRUE(allocated == 0);
    EXPECT_TRUE(perSymbol.load() == 1020000);
}

TEST(theCounterSeesAlignedNew) {
    long long before = g_allocations.load(std::memory_order_relaxed);
    void* p = ::operator new(64, std::align_val_t(64));
    EXPECT_TRUE(g_allocations.load(std::memory_order_relaxed) == before + 1);
    EXPECT_TRUE(reinterpret_cast<std::uintptr_t>(p) % 64 == 0);
    ::operator delete(p, std::align_val_t(64));
    // The ring's slot storage takes this route, so a ring built in a hot path
    // would now be seen.
    TickRingBuffer ring(8);
    EXPECT_TRUE(g_allocations.load(std::memory_order_relaxed) > before + 1);
}

QTEST_MAIN()
