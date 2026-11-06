// Port of com.quantfinlib.marketdata.TickRingBuffer (Java): a
// zero-allocation single-producer / single-consumer ring buffer for
// market data ticks, in the style of the LMAX Disruptor:
//
//  - Preallocated slots -- one 64-byte, cache-line-aligned struct per tick,
//    sized once in the constructor, so a message costs one line transfer
//    between cores and neighbouring messages never share a line. Nothing is
//    allocated per tick on either side. (The Java original uses parallel
//    arrays, which cost four lines per message.)
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

#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace quantfinlib {

class TickRingBuffer {
public:
    /** Bytes per slot: exactly one cache line. */
    static constexpr int SLOT_BYTES = 64;

    /** Largest accepted request; the capacity must stay a positive int. */
    static constexpr int MAX_CAPACITY = 1 << 30;

    /**
     * requestedCapacity is rounded up to the next power of two (min 4).
     * @throws std::invalid_argument above MAX_CAPACITY
     */
    explicit TickRingBuffer(int requestedCapacity)
            : capacity_(roundCapacity(requestedCapacity)),
              mask_(capacity_ - 1),
              slots_(static_cast<std::size_t>(capacity_)) {}

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
        Slot& slot = slots_[static_cast<std::size_t>(t & mask_)];
        slot.symbolId = symbolId;
        slot.price = price;
        slot.size = size;
        slot.timestampNanos = timestampNanos;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    /**
     * Consumer side; single consumer thread only. Drains up to `limit`
     * ticks into the sink (any callable taking symbolId, price, size,
     * timestampNanos) and returns how many were delivered. A limit of zero
     * or less delivers nothing. The sink must not throw: the batch would be
     * delivered again on the next drain.
     */
    template <typename Sink>
    int drainTo(Sink&& sink, int limit) {
        if (limit <= 0) {
            return 0;
        }
        std::int64_t h = head_.load(std::memory_order_relaxed);   // consumer-owned
        if (h >= cachedTail_) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (h >= cachedTail_) {
                return 0;
            }
        }
        int n = static_cast<int>(std::min<std::int64_t>(cachedTail_ - h, limit));
        for (int k = 0; k < n; k++) {
            const Slot& slot = slots_[static_cast<std::size_t>(h & mask_)];
            sink(slot.symbolId, slot.price, slot.size, slot.timestampNanos);
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
    struct alignas(SLOT_BYTES) Slot {
        double price;
        double size;
        std::int64_t timestampNanos;
        int symbolId;
    };
    static_assert(sizeof(Slot) == SLOT_BYTES && alignof(Slot) == SLOT_BYTES);

    // Java: Integer.highestOneBit(Math.max(2, requestedCapacity - 1)) * 2.
    static int roundCapacity(int requested) {
        if (requested > MAX_CAPACITY) {
            throw std::invalid_argument("ring capacity above MAX_CAPACITY");
        }
        unsigned v = static_cast<unsigned>(requested - 1 > 2 ? requested - 1 : 2);
        return static_cast<int>(std::bit_floor(v) * 2);
    }

    const int capacity_;
    const int mask_;
    std::vector<Slot> slots_;

    // Padded sequences and per-side caches: each on its own cache line
    // (the alignas(64) analogue of the Java padding-field runs).
    alignas(64) std::atomic<std::int64_t> head_{0};   // next slot to consume
    alignas(64) std::atomic<std::int64_t> tail_{0};   // next slot to fill
    alignas(64) std::int64_t cachedHead_ = 0;         // producer-local view of head
    alignas(64) std::int64_t cachedTail_ = 0;         // consumer-local view of tail
    alignas(64) char endPad_[64] = {};                // guard the trailing line
};

}  // namespace quantfinlib
