// Low-latency lane benchmarks. Every number printed here was measured by this
// program, on this host, in this run. It makes no comparison with any other
// language or run.
//
// Method
//  - Timing: start = lfence; rdtsc; lfence. End = rdtscp; lfence. The TSC is
//    calibrated against steady_clock. Off x86 a steady_clock read is used.
//  - Nothing is subtracted from any sample. Fences drain the pipeline, so the
//    empty-pair cost does not add linearly to the work between them, and
//    subtracting it reported 0 ns for real work. Instead the empty-pair
//    median is printed as the timer floor: a same-thread sample close to it
//    means the operation is below what this timer can resolve. Use the
//    unfenced batch mean for those.
//  - Cross-thread samples assume a synchronized, invariant TSC.
//  - Percentiles come from LatencyRecorder: log-linear buckets, so a value is
//    good to about 3%. "mean" is the exact mean of the samples.
//  - Random choices are made outside the timed region.
//  - A HiccupMonitor runs throughout. If its max is close to a stage's max,
//    the platform owns that tail.
//
// Workloads
//  - book: passive adds near the touch, cancels that always hit a live order,
//    and a small share of marketable IOCs that move the touch. The run fails
//    loudly if any cancel misses or any add hits POOL_FULL. Measured at 16k
//    and 1M resting orders in one book, and across many books.
//  - single-thread code path: tick ring, strategy, gate, order ring, book, all
//    on one thread. There is no parsing, network, encoding or core hop in it,
//    so it is a code-path cost, not a tick-to-order or wire-to-wire latency.
//  - ring and bus hand-off: one paced message at a time between two threads.
//    Bus runs are labelled busy-spin or parked; they differ by orders of
//    magnitude and must not be quoted without the label.
//
// Usage: ull_bench [--pin A,B] [--quick]
//   --pin A,B  pin the main thread to CPU A and the consumer thread to CPU B
//              (Linux only). Without it nothing is pinned.
//   --quick    about a tenth of the samples, and no 1M-order stages.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(__x86_64__) || defined(_M_X64)
#define QFL_BENCH_TSC 1
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#else
#define QFL_BENCH_TSC 0
#endif

#if defined(__linux__)
#include <sched.h>
#include <sys/utsname.h>
#endif

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
#include "quantfinlib/trading/order_ring_buffer.hpp"
#include "quantfinlib/util/hiccup_monitor.hpp"
#include "quantfinlib/util/latency_recorder.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"

using quantfinlib::HftMarketDataBus;
using quantfinlib::HftOrderBook;
using quantfinlib::HftRiskGate;
using quantfinlib::HiccupMonitor;
using quantfinlib::LatencyRecorder;
using quantfinlib::OrderRingBuffer;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;
using quantfinlib::TickRingBuffer;

namespace {

using Clock = std::chrono::steady_clock;

// ------------------------------------------------------------------
// Timer
// ------------------------------------------------------------------

#if QFL_BENCH_TSC
inline std::uint64_t timerStart() {
    _mm_lfence();
    std::uint64_t t = __rdtsc();
    _mm_lfence();
    return t;
}

inline std::uint64_t timerEnd() {
    unsigned aux;
    std::uint64_t t = __rdtscp(&aux);
    _mm_lfence();
    return t;
}
#else
inline std::uint64_t timerStart() {
    return static_cast<std::uint64_t>(Clock::now().time_since_epoch().count());
}
inline std::uint64_t timerEnd() { return timerStart(); }
#endif

volatile std::int64_t g_sink = 0;

double g_nsPerTick = 1.0;
std::uint64_t g_floorTicks = 0;

void calibrateTimer() {
    auto t0 = Clock::now();
    std::uint64_t c0 = timerStart();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    std::uint64_t c1 = timerEnd();
    auto t1 = Clock::now();
    g_nsPerTick = std::chrono::duration<double, std::nano>(t1 - t0).count()
            / static_cast<double>(c1 - c0);

    std::vector<std::uint64_t> d(1 << 16);
    for (auto& x : d) {
        std::uint64_t a = timerStart();
        std::uint64_t b = timerEnd();
        x = b - a;
    }
    std::nth_element(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(d.size() / 2), d.end());
    g_floorTicks = d[d.size() / 2];
}

inline std::int64_t sameThreadNs(std::uint64_t ticks) {
    return static_cast<std::int64_t>(static_cast<double>(ticks) * g_nsPerTick);
}

inline std::int64_t crossThreadNs(std::uint64_t from, std::uint64_t to) {
    // A small negative delta is TSC skew between cores; it records as zero.
    return to > from ? static_cast<std::int64_t>(static_cast<double>(to - from) * g_nsPerTick) : 0;
}

// ------------------------------------------------------------------
// Options, host, reporting
// ------------------------------------------------------------------

int g_pinMain = -1;
int g_pinConsumer = -1;
bool g_quick = false;

void pinThisThread(int cpu) {
#if defined(__linux__)
    if (cpu < 0) {
        return;
    }
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(cpu, &set);
    if (sched_setaffinity(0, sizeof(set), &set) != 0) {
        std::fprintf(stderr, "cannot pin to cpu %d\n", cpu);
        std::exit(2);
    }
#else
    (void)cpu;
#endif
}

void parseArgs(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        if (std::strcmp(argv[i], "--quick") == 0) {
            g_quick = true;
        } else if (std::strcmp(argv[i], "--pin") == 0 && i + 1 < argc
                   && std::sscanf(argv[++i], "%d,%d", &g_pinMain, &g_pinConsumer) == 2) {
#if !defined(__linux__)
            std::fprintf(stderr, "--pin is Linux only\n");
            std::exit(2);
#endif
        } else {
            std::fprintf(stderr, "usage: ull_bench [--pin A,B] [--quick]\n");
            std::exit(2);
        }
    }
}

std::string firstLineWith(const char* path, const char* key) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (line.find(key) != std::string::npos) {
            return line;
        }
    }
    return "";
}

void printHost() {
    std::printf("Low-latency lane benchmarks\n");
#if defined(__linux__)
    utsname u{};
    uname(&u);
    std::string model = firstLineWith("/proc/cpuinfo", "model name");
    auto colon = model.find(':');
    std::string version = firstLineWith("/proc/version", "");
    bool wsl = version.find("microsoft") != std::string::npos
            || version.find("Microsoft") != std::string::npos;
    std::printf("host:     %s %s %s\n", u.sysname, u.release, u.machine);
    std::printf("cpu:      %s, %u logical\n",
                colon == std::string::npos ? "unknown" : model.c_str() + colon + 2,
                std::thread::hardware_concurrency());
    if (wsl) {
        std::printf("virtual:  WSL2. vCPUs float over physical cores, so pinning here pins a\n"
                    "          vCPU only, and the Windows host can preempt it at any time.\n");
    }
#else
    std::printf("host:     not Linux, %u logical CPUs\n", std::thread::hardware_concurrency());
#endif
#if defined(__clang__)
    std::printf("compiler: clang %s\n", __clang_version__);
#elif defined(__GNUC__)
    std::printf("compiler: gcc %s\n", __VERSION__);
#elif defined(_MSC_VER)
    std::printf("compiler: msvc %d\n", _MSC_VER);
#endif
    if (g_pinMain >= 0) {
        std::printf("pinning:  main thread cpu %d, consumer thread cpu %d\n", g_pinMain,
                    g_pinConsumer);
    } else {
        std::printf("pinning:  NONE. Threads float; expect the tail to measure the scheduler.\n");
    }
    std::printf("timer:    %s, %.4f ns/tick, floor (empty pair) %.1f ns, not subtracted\n\n",
                QFL_BENCH_TSC ? "fenced rdtsc/rdtscp" : "steady_clock", g_nsPerTick,
                static_cast<double>(g_floorTicks) * g_nsPerTick);
    std::printf("%-44s %9s %8s %7s %7s %7s %8s %8s %9s\n", "stage (ns)", "n", "mean", "p50",
                "p90", "p99", "p99.9", "p99.99", "max");
}

void row(const char* name, const LatencyRecorder& r) {
    std::printf("%-44s %9lld %8.1f %7lld %7lld %7lld %8lld %8lld %9lld\n", name,
                static_cast<long long>(r.count()), r.mean(),
                static_cast<long long>(r.percentile(0.50)),
                static_cast<long long>(r.percentile(0.90)),
                static_cast<long long>(r.percentile(0.99)),
                static_cast<long long>(r.percentile(0.999)),
                static_cast<long long>(r.percentile(0.9999)),
                static_cast<long long>(r.max()));
}

void fail(const char* what) {
    std::fprintf(stderr, "workload invariant broken: %s\n", what);
    std::exit(3);
}

// ------------------------------------------------------------------
// Risk gate
// ------------------------------------------------------------------

void benchGate() {
    HftRiskGate gate(64);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .priceCollarPct(0.10);
    for (int s = 0; s < 64; s++) {
        gate.setReferencePrice(s, 100.0);
    }
    auto call = [&](int i) {
        return gate.check(i & 63, (i & 1) ? Side::BUY : Side::SELL, 1 + (i & 127),
                          100.0 + (i & 7) * 0.01);
    };
    int acc = 0;
    for (int i = 0; i < 2000000; i++) {
        acc += call(i);
    }
    const int n = g_quick ? 200000 : 2000000;
    auto b0 = Clock::now();
    for (int i = 0; i < n * 10; i++) {
        acc += call(i);
    }
    double batch = std::chrono::duration<double, std::nano>(Clock::now() - b0).count() / (n * 10.0);
    LatencyRecorder rec;
    for (int i = 0; i < n; i++) {
        std::uint64_t c0 = timerStart();
        int r = call(i);
        std::uint64_t c1 = timerEnd();
        acc += r;
        rec.record(sameThreadNs(c1 - c0));
    }
    g_sink = acc;
    row("gate check, one call, fenced", rec);
    std::printf("  unfenced batch mean %.1f ns/call: a throughput figure, calls overlap\n", batch);
}

// ------------------------------------------------------------------
// Book: near-touch flow
// ------------------------------------------------------------------

constexpr int MIN_T = 5000;
constexpr int MAX_T = 15000;
constexpr int MID_T = 10000;

/** One book plus the bookkeeping that keeps every cancel a real one. */
struct BookLane {
    BookLane(int restingTarget, std::size_t idSpace)
            : book(MIN_T, MAX_T, restingTarget + restingTarget / 4 + 1024),
              position(idSpace, -1) {
        alive.reserve(static_cast<std::size_t>(restingTarget) * 2);
        makers.reserve(1 << 12);
        book.tradeSink(&onTrade, this);
    }

    static void onTrade(void* ctx, std::int64_t maker, std::int64_t, int, std::int64_t,
                        std::int64_t) {
        auto* lane = static_cast<BookLane*>(ctx);
        lane->makers.push_back(maker);   // reserved: no allocation in the timed region
    }

    void track(std::int64_t id) {
        if (static_cast<std::size_t>(id) >= position.size()) {
            fail("id space too small");
        }
        position[static_cast<std::size_t>(id)] = static_cast<int>(alive.size());
        alive.push_back(id);
    }

    void untrack(std::int64_t id) {
        int at = position[static_cast<std::size_t>(id)];
        if (at < 0) {
            return;
        }
        std::int64_t last = alive.back();
        alive[static_cast<std::size_t>(at)] = last;
        position[static_cast<std::size_t>(last)] = at;
        alive.pop_back();
        position[static_cast<std::size_t>(id)] = -1;
    }

    void settleFills() {
        for (std::int64_t id : makers) {
            if (book.openQuantity(id) == 0) {
                untrack(id);
            }
        }
        makers.clear();
    }

    HftOrderBook book;
    std::vector<int> position;        // id -> index in alive, -1 when gone
    std::vector<std::int64_t> alive;
    std::vector<std::int64_t> makers;
    int mid = MID_T;
};

struct BookStats {
    std::int64_t adds = 0, cancels = 0, cancelMisses = 0, poolFull = 0, iocs = 0;
};

/** Distance from the touch: 0 about a third of the time, rarely past 30 ticks. */
int nearTouchOffset(SplittableRandom& rnd) {
    int d = 0;
    while (d < 60 && rnd.nextInt(3) != 0) {
        d++;
    }
    return d;
}

int passiveTick(const BookLane& lane, Side side, int offset) {
    if (side == Side::BUY) {
        int top = std::min(lane.mid - 1, lane.book.bestAskTick() - 1);
        return std::max(MIN_T, top - offset);
    }
    int bottom = std::max(lane.mid + 1, lane.book.bestBidTick() + 1);
    return std::min(MAX_T, bottom + offset);
}

template <bool Timed>
void bookOp(BookLane& lane, SplittableRandom& rnd, int target, std::int64_t ts, BookStats& st,
            LatencyRecorder* addRec, LatencyRecorder* cancelRec, LatencyRecorder* iocRec) {
    if (rnd.nextInt(64) == 0) {
        lane.mid = std::clamp(lane.mid + (rnd.nextBoolean() ? 1 : -1), MID_T - 2000, MID_T + 2000);
    }
    Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
    int roll = rnd.nextInt(100);
    int addShare = lane.alive.size() < static_cast<std::size_t>(target) ? 60 : 40;
    std::uint64_t c0 = 0;
    std::uint64_t c1 = 0;

    if (roll < 5 && !lane.alive.empty()) {
        // Marketable IOC: a few lots through the touch.
        std::int64_t qty = 1 + rnd.nextInt(60);
        int limit = side == Side::BUY ? MAX_T : MIN_T;
        if (Timed) c0 = timerStart();
        std::int64_t filled = lane.book.submitIoc(side, limit, qty, ts);
        if (Timed) c1 = timerEnd();
        g_sink = filled;
        st.iocs++;
        if (Timed) iocRec->record(sameThreadNs(c1 - c0));
        lane.settleFills();
    } else if (roll < 5 + addShare || lane.alive.empty()) {
        int tick = passiveTick(lane, side, nearTouchOffset(rnd));
        std::int64_t qty = 1 + rnd.nextInt(50);
        if (Timed) c0 = timerStart();
        std::int64_t id = lane.book.submitLimit(side, tick, qty, ts);
        if (Timed) c1 = timerEnd();
        st.adds++;
        if (id == HftOrderBook::REJECT_POOL_FULL) {
            st.poolFull++;
            return;
        }
        if (id <= 0) {
            fail("add rejected");
        }
        if (Timed) addRec->record(sameThreadNs(c1 - c0));
        lane.track(id);
        lane.settleFills();
    } else {
        std::int64_t id =
                lane.alive[static_cast<std::size_t>(rnd.nextInt(static_cast<int>(lane.alive.size())))];
        if (Timed) c0 = timerStart();
        bool hit = lane.book.cancel(id);
        if (Timed) c1 = timerEnd();
        st.cancels++;
        st.cancelMisses += hit ? 0 : 1;
        if (Timed) cancelRec->record(sameThreadNs(c1 - c0));
        lane.untrack(id);
    }
}

void benchBook(const char* label, int books, int restingPerBook, int measuredOps) {
    const int warmOps = measuredOps / 2;
    // A book consumes at most one id per op. Picks are uniform, so with many
    // books 4x the mean is ample; BookLane::track fails loudly if it is not.
    std::size_t opsPerBook = static_cast<std::size_t>(measuredOps + warmOps);
    if (books > 1) {
        opsPerBook = opsPerBook / static_cast<std::size_t>(books) * 4;
    }
    std::size_t idSpace = static_cast<std::size_t>(restingPerBook) * 3 + opsPerBook + 65536;
    std::vector<std::unique_ptr<BookLane>> lanes;
    for (int i = 0; i < books; i++) {
        lanes.push_back(std::make_unique<BookLane>(restingPerBook, idSpace));
    }
    SplittableRandom rnd(42);
    BookStats st;

    // Prefill straight to the target, as passive orders around the mid.
    for (auto& lane : lanes) {
        while (lane->alive.size() < static_cast<std::size_t>(restingPerBook)) {
            Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
            std::int64_t id = lane->book.submitLimit(
                    side, passiveTick(*lane, side, nearTouchOffset(rnd)), 1 + rnd.nextInt(50), 0);
            if (id <= 0) {
                fail("prefill rejected");
            }
            lane->track(id);
        }
    }
    auto pick = [&]() -> BookLane& {
        return *lanes[static_cast<std::size_t>(books == 1 ? 0 : rnd.nextInt(books))];
    };
    for (int i = 0; i < warmOps; i++) {
        bookOp<false>(pick(), rnd, restingPerBook, i, st, nullptr, nullptr, nullptr);
    }

    st = BookStats{};
    LatencyRecorder add;
    LatencyRecorder cancel;
    LatencyRecorder ioc;
    auto t0 = Clock::now();
    for (int i = 0; i < measuredOps; i++) {
        bookOp<true>(pick(), rnd, restingPerBook, i, st, &add, &cancel, &ioc);
    }
    double wall = std::chrono::duration<double>(Clock::now() - t0).count();
    if (st.cancelMisses != 0) {
        fail("a cancel missed");
    }
    if (st.poolFull != 0) {
        fail("POOL_FULL during the measured run");
    }
    std::int64_t resting = 0;
    std::int64_t trades = 0;
    for (auto& lane : lanes) {
        resting += lane->book.restingOrders();
        trades += lane->book.tradeCount();
    }
    std::string a = std::string(label) + " add";
    std::string c = std::string(label) + " cancel";
    std::string x = std::string(label) + " IOC";
    row(a.c_str(), add);
    row(c.c_str(), cancel);
    row(x.c_str(), ioc);
    std::printf("  %d book(s), resting %lld at end, adds %lld, cancels %lld (0 missed), "
                "IOCs %lld, trades %lld, POOL_FULL 0, %.0f ops/s incl. harness\n",
                books, static_cast<long long>(resting), static_cast<long long>(st.adds),
                static_cast<long long>(st.cancels), static_cast<long long>(st.iocs),
                static_cast<long long>(trades), measuredOps / wall);
}

// ------------------------------------------------------------------
// Cross-thread hand-offs
// ------------------------------------------------------------------

/** One message per ~2 us, so a sample is hand-off latency, not queue wait. */
template <typename Publish>
void pacedProducer(int samples, Publish&& publish) {
    const auto gap = static_cast<std::uint64_t>(2000.0 / g_nsPerTick);
    for (int i = 0; i < samples; i++) {
        while (!publish(i, static_cast<std::int64_t>(timerEnd()))) {
        }
        std::uint64_t until = timerEnd() + gap;
        while (timerEnd() < until) {
        }
    }
}

void benchRingHandoff() {
    const int samples = g_quick ? 50000 : 500000;
    TickRingBuffer ring(1 << 10);
    LatencyRecorder rec;
    std::atomic<int> received{0};
    std::thread consumer([&] {
        pinThisThread(g_pinConsumer);
        int seen = 0;
        while (seen < samples) {
            seen += ring.drainTo(
                    [&](int, double, double, std::int64_t sent) {
                        rec.record(crossThreadNs(static_cast<std::uint64_t>(sent), timerEnd()));
                    },
                    16);
        }
        received.store(seen, std::memory_order_release);
    });
    pacedProducer(samples, [&](int i, std::int64_t now) {
        return ring.publish(i & 31, 100.0, 1.0, now);
    });
    consumer.join();
    row("ring hand-off, busy-spin consumer", rec);
}

void benchBusHandoff(bool busySpin) {
    const int samples = g_quick ? 50000 : (busySpin ? 500000 : 200000);
    HftMarketDataBus bus(1 << 10, 4, busySpin);
    int symbol = bus.registerSymbol("BENCH");
    LatencyRecorder rec;
    std::atomic<int> received{0};
    bool pinned = false;
    bus.subscribeAll([&](int, double, double, std::int64_t sent) {
        if (!pinned) {
            pinThisThread(g_pinConsumer);
            pinned = true;
        }
        rec.record(crossThreadNs(static_cast<std::uint64_t>(sent), timerEnd()));
        received.fetch_add(1, std::memory_order_release);
    });
    bus.start();
    pacedProducer(samples, [&](int, std::int64_t now) {
        return bus.publish(symbol, 100.0, 1.0, now);
    });
    while (received.load(std::memory_order_acquire) < samples) {
        std::this_thread::yield();
    }
    bus.close();
    row(busySpin ? "bus publish->listener, BUSY-SPIN consumer"
                 : "bus publish->listener, PARKED consumer",
        rec);
}

// ------------------------------------------------------------------
// Single-thread code path
// ------------------------------------------------------------------

void benchSingleThreadPath() {
    // A two-sided quoter around a wandering price: each tick places one
    // quote one tick off the price (some cross once the price has moved), and
    // the oldest quote is cancelled once 4096 are out, so the pool never
    // fills. Runs on one thread with no parsing, network, encoding or core hop.
    constexpr int SYMBOL = 3;
    constexpr int WINDOW = 4096;
    TickRingBuffer ticks(1 << 10);
    OrderRingBuffer orders(1 << 10);
    HftRiskGate gate(8);
    gate.maxOrderQuantity(10000).maxOrderNotional(1e9).maxPositionQuantity(1000000)
        .priceCollarPct(0.10);
    gate.setReferencePrice(SYMBOL, 100.0);
    HftOrderBook book(9000, 11000, WINDOW * 2);
    book.tradeSink([](void*, std::int64_t, std::int64_t, int, std::int64_t, std::int64_t) {},
                   nullptr);

    SplittableRandom rnd(7);
    std::vector<std::int64_t> quotes(WINDOW, 0);
    std::int64_t sent = 0;
    std::int64_t placed = 0;
    std::int64_t gateRejects = 0;
    std::int64_t poolFull = 0;
    std::int64_t cancelHits = 0;
    int priceTick = 10000;
    bool buy = true;

    auto strategy = [&](int symbolId, double price, double, std::int64_t ts) {
        Side side = buy ? Side::BUY : Side::SELL;
        buy = !buy;
        double quote = price + (side == Side::BUY ? -0.01 : 0.01);
        if (gate.check(symbolId, side, 10, quote) == HftRiskGate::OK) {
            orders.publish(++sent, symbolId, side, 10, quote, ts);
        } else {
            gateRejects++;
        }
    };
    auto venue = [&](std::int64_t, int, Side side, std::int64_t qty, double price,
                     std::int64_t ts) {
        std::size_t slot = static_cast<std::size_t>(placed % WINDOW);
        if (quotes[slot] != 0) {
            cancelHits += book.cancel(quotes[slot]) ? 1 : 0;
        }
        std::int64_t id = book.submitLimit(side, static_cast<int>(std::llround(price * 100)), qty, ts);
        poolFull += id == HftOrderBook::REJECT_POOL_FULL ? 1 : 0;
        quotes[slot] = id > 0 ? id : 0;
        placed++;
    };

    auto nextTick = [&] {
        priceTick = std::clamp(priceTick + rnd.nextInt(3) - 1, 9900, 10100);
        return priceTick / 100.0;
    };
    for (int i = 0; i < 1000000; i++) {
        ticks.publish(SYMBOL, nextTick(), 1.0, i);
        ticks.drainTo(strategy, 4);
        orders.drainTo(venue, 4);
    }
    const int n = g_quick ? 200000 : 2000000;
    LatencyRecorder rec;
    poolFull = 0;
    gateRejects = 0;
    for (int i = 0; i < n; i++) {
        double px = nextTick();
        std::uint64_t c0 = timerStart();
        ticks.publish(SYMBOL, px, 1.0, i);
        ticks.drainTo(strategy, 4);
        orders.drainTo(venue, 4);
        std::uint64_t c1 = timerEnd();
        rec.record(sameThreadNs(c1 - c0));
    }
    if (poolFull != 0) {
        fail("POOL_FULL in the single-thread path");
    }
    row("single-thread code path (no I/O, no core hop)", rec);
    std::printf("  resting %d, trades %lld, cancels hit %lld, gate rejects %lld, POOL_FULL 0\n",
                book.restingOrders(), static_cast<long long>(book.tradeCount()),
                static_cast<long long>(cancelHits), static_cast<long long>(gateRejects));
}

}  // namespace

int main(int argc, char** argv) {
    parseArgs(argc, argv);
    pinThisThread(g_pinMain);
    calibrateTimer();
    printHost();

    HiccupMonitor hiccups;
    hiccups.start();

    benchGate();
    const int ops = g_quick ? 400000 : 4000000;
    benchBook("book 16k", 1, 1 << 14, ops);
    if (!g_quick) {
        benchBook("book 1M", 1, 1 << 20, ops);
        benchBook("book 64x16k", 64, 1 << 14, ops);
    }
    benchRingHandoff();
    benchBusHandoff(true);
    benchBusHandoff(false);
    benchSingleThreadPath();

    hiccups.close();
    std::printf("\n%s\n", hiccups.summary().c_str());
    return 0;
}
