// The C++ analogue of the Java allocation-counter tests
// (HftOrderBookTest.steadyStateOperationsAreAllocationFree and
// HftOrderBookTifTest.tifOperationsAreAllocationFree, which assert via
// ThreadMXBean.getThreadAllocatedBytes that the hot path allocates
// nothing). Here the whole test binary's global operator new/new[] is
// replaced with counting versions; after warm-up, one million mixed
// steady-state operations across the entire hot path (book limit/IOC/
// FOK/post-only/cancel, risk-gate check + onFill, order-ring publish +
// drain, latency-recorder record) must perform EXACTLY ZERO allocations
// -- stricter than Java's <100KB slack, because C++ has no JIT to appease.
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <new>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"
#include "quantfinlib/trading/hft_quoter.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"
#include "quantfinlib/util/latency_recorder.hpp"
#include "qtest.hpp"

// ------------------------------------------------------------------
// Counting global allocator: every route into the heap bumps the counter.
// ------------------------------------------------------------------

namespace {
std::atomic<long long> g_allocations{0};
}

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

using quantfinlib::HftOrderBook;
using quantfinlib::HftRiskGate;
using quantfinlib::OrderRingBuffer;
using quantfinlib::LatencyRecorder;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;

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
int step(HftOrderBook& b, HftRiskGate& gate, OrderRingBuffer& ring,
         LatencyRecorder& rec, SplittableRandom& rnd, std::int64_t* ids, int idCount,
         int idCap, int i) {
    Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
    int tick = MIN_T + rnd.nextInt(MAX_T - MIN_T + 1);
    std::int64_t qty = 1 + rnd.nextInt(50);

    if (gate.check(0, side, qty, tick / 100.0) == HftRiskGate::OK) {
        ring.publish(i, 0, side, qty, tick / 100.0, i);
    }
    ring.drainTo([](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {}, 8);

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
            std::numeric_limits<std::int64_t>::max()).disableReferenceCheck();
    OrderRingBuffer ring(1 << 10);
    LatencyRecorder rec;
    SplittableRandom rnd(42);

    constexpr int ID_CAP = 1024;
    static std::int64_t ids[ID_CAP];  // preallocated: the TEST must not allocate either
    int idCount = 0;

    // Warm-up: populate the book, fault every array page, exercise all paths.
    for (int i = 0; i < 200000; i++) {
        idCount = step(book, gate, ring, rec, rnd, ids, idCount, ID_CAP, i);
    }

    long long before = g_allocations.load(std::memory_order_relaxed);
    for (int i = 0; i < 1000000; i++) {
        idCount = step(book, gate, ring, rec, rnd, ids, idCount, ID_CAP, i);
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

TEST(orderLifecycleAllocatesNothing) {
    // Submit, deliver, ack, amend, part-fill, cancel, plus a two-sided quote,
    // on one thread through pollOnce so every step is on the counted path.
    using quantfinlib::HftOrderGateway;
    using quantfinlib::HftQuoter;
    using quantfinlib::VenueMessage;
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(1000000000)
        .priceCollarPct(0.5);
    gate.setReferencePrice(0, 100.0);
    gate.setReferencePrice(1, 100.0);
    HftOrderGateway gw(1 << 10, gate, false);
    static int ackCount;
    ackCount = 0;
    gw.addVenueListener([&gw](const VenueMessage& m) {
        switch (m.type) {
            case VenueMessage::Type::NEW: gw.onOrderAck(m.clOrdId); break;
            case VenueMessage::Type::REPLACE: gw.onReplaceAck(m.clOrdId); break;
            case VenueMessage::Type::CANCEL: gw.onCancelAck(m.clOrdId); break;
        }
        ackCount++;
    });
    HftQuoter quoter(gw, 4, HftQuoter::Config::of(10, 0.05));
    auto cycle = [&](int i) {
        std::int64_t id = gw.submit(1, Side::BUY, 100, 100.0, i);
        gw.pollOnce();
        gw.replace(id, 80, 100.5, i);
        gw.pollOnce();
        gw.onFill(id, 1, Side::BUY, 10);
        gw.cancel(id, i);
        gw.pollOnce();
        gate.onFill(1, Side::SELL, 10);
        quoter.onTick(0, 100.0 + (i % 5) * 0.01, 1, i);
        gw.pollOnce();
    };
    for (int i = 0; i < 1000; i++) {
        cycle(i);
    }
    long long before = g_allocations.load(std::memory_order_relaxed);
    for (int i = 0; i < 200000; i++) {
        cycle(i);
    }
    long long after = g_allocations.load(std::memory_order_relaxed);
    EXPECT_TRUE(after == before);
    EXPECT_TRUE(ackCount > 200000);
}

QTEST_MAIN()
