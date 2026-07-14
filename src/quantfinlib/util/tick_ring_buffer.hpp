// Port of com.quantfinlib.marketdata.TickRingBuffer (Java): a
// zero-allocation single-producer / single-consumer ring buffer for
// market data ticks, in the style of the LMAX Disruptor:
//
//  - Preallocated primitive slots -- ticks live in parallel arrays sized
//    once in the constructor; nothing is allocated per tick on either
//    side.
//  - Cache-line-padded sequences -- head and tail counters are alignas(64)
//    (the Java @Contended-style padding fields) so producer and consumer
//    cores never false-share.
//  - Acquire/release ordering -- slot writes are published with a single
//    release store of the tail, exactly the Java VarHandle discipline;
//    no CAS, no locks on the hot path.
//  - Sequence caching -- each side caches the other's sequence and only
//    re-reads the atomic counter when it appears blocked, removing most
//    cross-core traffic.
#pragma once

#include <atomic>
#include <cstdint>
#include <vector>

namespace quantfinlib {

class TickRingBuffer {
public:
    /** requestedCapacity is rounded up to the next power of two (min 4). */
    explicit TickRingBuffer(int requestedCapacity)
            : capacity_(roundCapacity(requestedCapacity)),
              mask_(capacity_ - 1),
              symbolIds_(capacity_),
              prices_(capacity_),
              sizes_(capacity_),
              timestamps_(capacity_) {}

    /**
     * Producer side; single producer thread only. Returns false when full --
     * the caller chooses the backpressure policy (spin, drop, or count).
     */
    bool publish(int symbolId, double price, double size, std::int64_t timestampNanos) {
        std::int64_t t = tail_.load(std::memory_order_relaxed);   // producer-owned
        if (t - cachedHead_ >= capacity_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (t - cachedHead_ >= capacity_) {
                return false;
            }
        }
        int i = static_cast<int>(t & mask_);
        symbolIds_[i] = symbolId;
        prices_[i] = price;
        sizes_[i] = size;
        timestamps_[i] = timestampNanos;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    /**
     * Consumer side; single consumer thread only. Drains up to `limit`
     * ticks into the sink (any callable taking symbolId, price, size,
     * timestampNanos) and returns how many were delivered.
     */
    template <typename Sink>
    int drainTo(Sink&& sink, int limit) {
        std::int64_t h = head_.load(std::memory_order_relaxed);   // consumer-owned
        if (h >= cachedTail_) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (h >= cachedTail_) {
                return 0;
            }
        }
        int n = static_cast<int>(std::min<std::int64_t>(cachedTail_ - h, limit));
        for (int k = 0; k < n; k++) {
            int i = static_cast<int>(h & mask_);
            sink(symbolIds_[i], prices_[i], sizes_[i], timestamps_[i]);
            h++;
        }
        head_.store(h, std::memory_order_release);
        return n;
    }

    bool isEmpty() const {
        return head_.load(std::memory_order_acquire) >= tail_.load(std::memory_order_acquire);
    }

    int capacity() const { return capacity_; }

private:
    // Java: Integer.highestOneBit(Math.max(2, requestedCapacity - 1)) * 2.
    static int roundCapacity(int requested) {
        unsigned v = static_cast<unsigned>(requested - 1 > 2 ? requested - 1 : 2);
        unsigned high = 1u << (31 - __builtin_clz(v));
        return static_cast<int>(high * 2);
    }

    const int capacity_;
    const int mask_;
    std::vector<int> symbolIds_;
    std::vector<double> prices_;
    std::vector<double> sizes_;
    std::vector<std::int64_t> timestamps_;

    // Padded sequences and per-side caches: each on its own cache line
    // (the alignas(64) analogue of the Java padding-field runs).
    alignas(64) std::atomic<std::int64_t> head_{0};   // next slot to consume
    alignas(64) std::atomic<std::int64_t> tail_{0};   // next slot to fill
    alignas(64) std::int64_t cachedHead_ = 0;         // producer-local view of head
    alignas(64) std::int64_t cachedTail_ = 0;         // consumer-local view of tail
    alignas(64) char endPad_[64] = {};                // guard the trailing line
};

}  // namespace quantfinlib
