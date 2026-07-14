// Ultra-low-latency benchmarks: the C++ mirror of the Java project's
// HftOrderBenchmark / HftQuoterBenchmark (docs/ULTRA_LOW_LATENCY.md).
// Java baselines on the same hardware class: risk-gate check ~3 ns,
// book mixed add/cancel ~204 ns median, ring publish->consume p50
// ~200 ns, tick->order end-to-end p50 504 ns / p99 1 us.
//
// Method: for each stage, (1) a steady_clock-timed batch gives the
// throughput-style mean ns/op, and (2) per-op __builtin_ia32_rdtsc()
// deltas (TSC calibrated against steady_clock, measured rdtsc-pair
// overhead subtracted) feed a LatencyRecorder for median and p99.
//
// CAVEAT printed at runtime: this is Windows, unpinned cores, no
// isolcpus/priority tuning -- scheduler preemptions and SMIs of ~ms own
// the far tail, so p99.9/max here measure the platform, not the code.
// Compare medians (and the Java numbers are JIT-warmed Linux medians).
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"
#include "quantfinlib/util/latency_recorder.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"

using quantfinlib::HftMarketDataBus;
using quantfinlib::HftOrderBook;
using quantfinlib::HftRiskGate;
using quantfinlib::LatencyRecorder;
using quantfinlib::OrderRingBuffer;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;
using quantfinlib::TickRingBuffer;

namespace {

using Clock = std::chrono::steady_clock;

inline std::uint64_t rdtsc() { return __builtin_ia32_rdtsc(); }

template <typename T>
inline void blackhole(T v) {
    asm volatile("" : : "r"(v) : "memory");
}

double g_nsPerCycle = 0.0;

/** TSC frequency via steady_clock over ~200 ms. */
void calibrateTsc() {
    auto t0 = Clock::now();
    std::uint64_t c0 = rdtsc();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::uint64_t c1 = rdtsc();
    auto t1 = Clock::now();
    double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    g_nsPerCycle = ns / static_cast<double>(c1 - c0);
}

/** Median cost of an empty rdtsc pair, in cycles (subtracted from samples). */
std::uint64_t rdtscOverheadCycles() {
    std::vector<std::uint64_t> d(4096);
    for (auto& x : d) {
        std::uint64_t a = rdtsc();
        std::uint64_t b = rdtsc();
        x = b - a;
    }
    std::nth_element(d.begin(), d.begin() + d.size() / 2, d.end());
    return d[d.size() / 2];
}

std::int64_t toNs(std::uint64_t cycles, std::uint64_t overhead) {
    std::uint64_t net = cycles > overhead ? cycles - overhead : 0;
    return static_cast<std::int64_t>(static_cast<double>(net) * g_nsPerCycle);
}

void report(const char* name, double batchNsPerOp, const LatencyRecorder& rec,
            const char* javaBaseline) {
    std::printf("%-28s batch %8.1f ns/op (%7.2f Mops/s) | rdtsc p50 %5lld ns  p99 %7lld ns | Java: %s\n",
                name, batchNsPerOp, 1e3 / batchNsPerOp,
                static_cast<long long>(rec.percentile(0.50)),
                static_cast<long long>(rec.percentile(0.99)), javaBaseline);
}

// ------------------------------------------------------------------
// (a) Risk-gate check
// ------------------------------------------------------------------

void benchGateCheck() {
    HftRiskGate gate(64);
    gate.maxOrderQuantity(10000)
        .maxOrderNotional(1e9)
        .maxPositionQuantity(1000000)
        .priceCollarPct(0.10);
    for (int s = 0; s < 64; s++) {
        gate.setReferencePrice(s, 100.0);
    }

    // Warm-up.
    int acc = 0;
    for (int i = 0; i < 2000000; i++) {
        acc += gate.check(i & 63, (i & 1) ? Side::BUY : Side::SELL, 1 + (i & 127),
                          100.0 + (i & 7) * 0.01);
    }
    blackhole(acc);

    constexpr int N = 20000000;
    auto t0 = Clock::now();
    acc = 0;
    for (int i = 0; i < N; i++) {
        acc += gate.check(i & 63, (i & 1) ? Side::BUY : Side::SELL, 1 + (i & 127),
                          100.0 + (i & 7) * 0.01);
    }
    auto t1 = Clock::now();
    blackhole(acc);
    double batch = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / N;

    LatencyRecorder rec;
    std::uint64_t ovh = rdtscOverheadCycles();
    for (int i = 0; i < 200000; i++) {
        std::uint64_t c0 = rdtsc();
        int r = gate.check(i & 63, (i & 1) ? Side::BUY : Side::SELL, 1 + (i & 127),
                           100.0 + (i & 7) * 0.01);
        std::uint64_t c1 = rdtsc();
        blackhole(r);
        rec.record(toNs(c1 - c0, ovh));
    }
    report("gate check", batch, rec, "~3 ns");
}

// ------------------------------------------------------------------
// (b) Book mixed add/cancel
// ------------------------------------------------------------------

constexpr int MIN_T = 9000;
constexpr int MAX_T = 11000;

void sinkNoop(void*, std::int64_t, std::int64_t, int, std::int64_t, std::int64_t) {}

int bookStep(HftOrderBook& b, SplittableRandom& rnd, std::int64_t* ids, int idCount,
             int idCap, int op) {
    if (rnd.nextInt(10) < 7 || idCount == 0) {
        Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
        int tick = MIN_T + rnd.nextInt(MAX_T - MIN_T + 1);
        std::int64_t id = b.submitLimit(side, tick, 1 + rnd.nextInt(50), op);
        if (id > 0 && b.openQuantity(id) > 0 && idCount < idCap) {
            ids[idCount++] = id;
        }
    } else {
        int pick = rnd.nextInt(idCount);
        b.cancel(ids[pick]);
        ids[pick] = ids[--idCount];
    }
    return idCount;
}

void benchBookMixed() {
    HftOrderBook b(MIN_T, MAX_T, 1 << 14);
    b.tradeSink(&sinkNoop, nullptr);
    SplittableRandom rnd(42);
    constexpr int ID_CAP = 4096;
    static std::int64_t ids[ID_CAP];
    int idCount = 0;

    for (int i = 0; i < 1000000; i++) {  // warm-up: steady-state book
        idCount = bookStep(b, rnd, ids, idCount, ID_CAP, i);
    }

    constexpr int N = 5000000;
    auto t0 = Clock::now();
    for (int i = 0; i < N; i++) {
        idCount = bookStep(b, rnd, ids, idCount, ID_CAP, i);
    }
    auto t1 = Clock::now();
    double batch = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / N;

    LatencyRecorder rec;
    std::uint64_t ovh = rdtscOverheadCycles();
    for (int i = 0; i < 200000; i++) {
        std::uint64_t c0 = rdtsc();
        idCount = bookStep(b, rnd, ids, idCount, ID_CAP, i);
        std::uint64_t c1 = rdtsc();
        rec.record(toNs(c1 - c0, ovh));
    }
    std::printf("  (book after run: resting=%d trades=%lld)\n", b.restingOrders(),
                static_cast<long long>(b.tradeCount()));
    report("book mixed add/cancel", batch, rec, "~204 ns median, 7M+ ops/s");
}

// ------------------------------------------------------------------
// (c) Ring publish -> consume (cross-thread SPSC hand-off)
// ------------------------------------------------------------------

void benchRingHandoff() {
    TickRingBuffer ring(1 << 10);
    constexpr int SAMPLES = 200000;
    LatencyRecorder rec;
    std::uint64_t ovh = rdtscOverheadCycles();

    std::atomic<bool> stop{false};
    std::atomic<int> received{0};
    std::thread consumer([&] {
        while (!stop.load(std::memory_order_acquire)) {
            ring.drainTo(
                    [&](int, double, double, std::int64_t sentTsc) {
                        std::uint64_t now = rdtsc();
                        rec.record(toNs(now - static_cast<std::uint64_t>(sentTsc), ovh));
                        received.fetch_add(1, std::memory_order_relaxed);
                    },
                    16);
        }
    });

    // Paced producer: one message per ~2 us so each sample measures the
    // hand-off latency, not queue wait inside a burst.
    for (int i = 0; i < SAMPLES; i++) {
        while (!ring.publish(i & 31, 100.0, 1.0,
                             static_cast<std::int64_t>(rdtsc()))) {
        }
        std::uint64_t until = rdtsc()
                + static_cast<std::uint64_t>(2000.0 / g_nsPerCycle);
        while (rdtsc() < until) {
        }
    }
    while (received.load(std::memory_order_relaxed) < SAMPLES) {
    }
    stop.store(true, std::memory_order_release);
    consumer.join();

    std::printf("%-28s cross-thread             | rdtsc p50 %5lld ns  p99 %7lld ns | Java: publish->strategy p50 204 ns\n",
                "ring publish->consume",
                static_cast<long long>(rec.percentile(0.50)),
                static_cast<long long>(rec.percentile(0.99)));
}

// ------------------------------------------------------------------
// (d) Tick -> strategy via the full HftMarketDataBus (Phase 5A)
// ------------------------------------------------------------------

void benchBusTickToStrategy() {
    // Same cross-thread hand-off measurement as (c), but through the real
    // HftMarketDataBus -- SymbolRegistry-resolved id, the bus's own
    // consumer thread/loop, and subscribeAll dispatch -- rather than a bare
    // TickRingBuffer, so this is the Java HftMarketDataBusBenchmark
    // "publish -> strategy" number's direct C++ counterpart.
    HftMarketDataBus bus(1 << 10, 4, false);
    int symbol = bus.registerSymbol("BENCH");
    constexpr int SAMPLES = 200000;
    LatencyRecorder rec;
    std::uint64_t ovh = rdtscOverheadCycles();
    std::atomic<int> received{0};

    bus.subscribeAll([&](int, double, double, std::int64_t sentTsc) {
        std::uint64_t now = rdtsc();
        rec.record(toNs(now - static_cast<std::uint64_t>(sentTsc), ovh));
        received.fetch_add(1, std::memory_order_relaxed);
    });
    bus.start();

    // Paced producer: one message per ~2 us so each sample measures the
    // hand-off latency, not queue wait inside a burst (same pacing as (c)).
    for (int i = 0; i < SAMPLES; i++) {
        while (!bus.publish(symbol, 100.0, 1.0, static_cast<std::int64_t>(rdtsc()))) {
        }
        std::uint64_t until = rdtsc()
                + static_cast<std::uint64_t>(2000.0 / g_nsPerCycle);
        while (rdtsc() < until) {
        }
    }
    while (received.load(std::memory_order_relaxed) < SAMPLES) {
    }
    bus.close();

    std::printf("%-28s cross-thread (via bus)   | rdtsc p50 %5lld ns  p99 %7lld ns | Java: publish->strategy p50 204 ns\n",
                "bus tick->strategy",
                static_cast<long long>(rec.percentile(0.50)),
                static_cast<long long>(rec.percentile(0.99)));
}

// ------------------------------------------------------------------
// (e) Tick-to-order composite (single-threaded pipeline walk)
// ------------------------------------------------------------------

void benchTickToOrder() {
    // The participant-side pipeline the Java 504 ns number describes:
    // tick ring -> strategy decision -> risk gate -> order ring -> venue
    // adapter submits into the (local, venue-grade) book. Walked on one
    // thread so the composite is pure code cost, no cross-core hops.
    TickRingBuffer ticks(1 << 10);
    OrderRingBuffer orders(1 << 10);
    HftRiskGate gate(8);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .priceCollarPct(0.10);
    gate.setReferencePrice(3, 100.0);
    HftOrderBook book(MIN_T, MAX_T, 1 << 14);
    book.tradeSink(&sinkNoop, nullptr);

    SplittableRandom rnd(7);
    std::int64_t sent = 0;

    auto strategy = [&](int symbolId, double price, double, std::int64_t ts) {
        // Random-direction quote around the tick + gate + order publication
        // (the same crossing-heavy op mix as the equivalence test, so the
        // venue below does real matching, not empty-book walks).
        Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
        std::int64_t qty = 1 + rnd.nextInt(50);
        if (gate.check(symbolId, side, qty, price) == HftRiskGate::OK) {
            orders.publish(++sent, symbolId, side, qty, price, ts);
        }
    };
    auto venue = [&](std::int64_t, int, Side side, std::int64_t qty, double price,
                     std::int64_t ts) {
        book.submitLimit(side, static_cast<int>(std::llround(price * 100)), qty, ts);
        gate.onFill(3, side, 1);
    };

    auto onePass = [&](int i) {
        double px = 99.0 + rnd.nextInt(201) * 0.01;  // 99.00..101.00, both sides
        ticks.publish(3, px, 1.0, i);
        ticks.drainTo(strategy, 4);
        orders.drainTo(venue, 4);
    };

    for (int i = 0; i < 1000000; i++) {  // warm-up
        onePass(i);
    }

    constexpr int N = 2000000;
    auto t0 = Clock::now();
    for (int i = 0; i < N; i++) {
        onePass(i);
    }
    auto t1 = Clock::now();
    double batch = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count()) / N;

    LatencyRecorder rec;
    std::uint64_t ovh = rdtscOverheadCycles();
    for (int i = 0; i < 200000; i++) {
        std::uint64_t c0 = rdtsc();
        onePass(i);
        std::uint64_t c1 = rdtsc();
        rec.record(toNs(c1 - c0, ovh));
    }
    std::printf("  (venue book after run: resting=%d trades=%lld orders=%lld)\n",
                book.restingOrders(), static_cast<long long>(book.tradeCount()),
                static_cast<long long>(book.orderCount()));
    report("tick-to-order composite", batch, rec, "p50 504 ns, p99 1 us");
}

}  // namespace

int main() {
    calibrateTsc();
    std::printf("ULL benchmarks (C++ port) -- TSC %.4f ns/cycle (%.2f GHz)\n",
                g_nsPerCycle, 1.0 / g_nsPerCycle);
    std::printf("CAVEAT: Windows, unpinned cores, no RT priority -- scheduler\n"
                "preemptions (~ms) own p99.9+/max; compare medians. Java\n"
                "baselines are JIT-warmed medians from docs/ULTRA_LOW_LATENCY.md.\n\n");
    benchGateCheck();
    benchBookMixed();
    benchRingHandoff();
    benchBusTickToStrategy();
    benchTickToOrder();
    return 0;
}
