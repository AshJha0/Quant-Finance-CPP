// Port of com.quantfinlib.trading.OrderRingBuffer (Java): zero-allocation
// single-producer / single-consumer ring buffer for order messages -- the
// order-entry mirror of the market-data TickRingBuffer: preallocated
// slots of exactly one cache line each, so a message costs one line where
// parallel arrays cost six, cache-line-padded (alignas(64)) sequences,
// acquire/release publication, and producer/consumer sequence caching.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class OrderRingBuffer {
public:
    /** requestedCapacity is rounded up to the next power of two (min 4). */
    explicit OrderRingBuffer(int requestedCapacity)
            : capacity_(roundCapacity(requestedCapacity)),
              mask_(capacity_ - 1),
              slots_(static_cast<std::size_t>(capacity_)) {}

    /** Producer side (trading thread). Returns false when full. */
    bool publish(std::int64_t orderId, int symbolId, Side side, std::int64_t quantity,
                 double price, std::int64_t timestampNanos) {
        return publishMessage(0, orderId, 0, symbolId, side, quantity, price, timestampNanos);
    }

    /**
     * Producer side, typed form: `type` and `refId` travel with the slot so
     * cancels and replaces stay in sequence behind the orders they refer to.
     * HftOrderGateway serialises its producers with its own lock.
     */
    bool publishMessage(std::uint8_t type, std::int64_t orderId, std::int64_t refId, int symbolId,
                        Side side, std::int64_t quantity, double price,
                        std::int64_t timestampNanos) {
        std::int64_t t = tail_.load(std::memory_order_relaxed);   // producer-owned
        if (t - cachedHead_ >= capacity_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (t - cachedHead_ >= capacity_) {
                return false;
            }
        }
        Slot& s = slots_[static_cast<std::size_t>(t & mask_)];
        s.orderId = orderId;
        s.refId = refId;
        s.quantity = quantity;
        s.price = price;
        s.timestampNanos = timestampNanos;
        s.symbolId = symbolId;
        s.side = side;
        s.type = type;
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }

    /**
     * Consumer side (venue thread). Drains up to `limit` orders into the
     * sink (any callable taking orderId, symbolId, Side, quantity, price,
     * timestampNanos) and returns how many were delivered.
     */
    template <typename Sink>
    int drainTo(Sink&& sink, int limit) {
        return drainMessagesTo(
                [&sink](std::uint8_t, std::int64_t orderId, std::int64_t, int symbolId, Side side,
                        std::int64_t quantity, double price, std::int64_t timestampNanos) {
                    sink(orderId, symbolId, side, quantity, price, timestampNanos);
                },
                limit);
    }

    /** As drainTo, for a sink that also takes (type, orderId, refId, ...). */
    template <typename Sink>
    int drainMessagesTo(Sink&& sink, int limit) {
        std::int64_t h = head_.load(std::memory_order_relaxed);   // consumer-owned
        if (h >= cachedTail_) {
            cachedTail_ = tail_.load(std::memory_order_acquire);
            if (h >= cachedTail_) {
                return 0;
            }
        }
        int n = static_cast<int>(std::min<std::int64_t>(cachedTail_ - h, limit));
        for (int k = 0; k < n; k++) {
            const Slot& s = slots_[static_cast<std::size_t>(h & mask_)];
            sink(s.type, s.orderId, s.refId, s.symbolId, s.side, s.quantity, s.price,
                 s.timestampNanos);
            h++;
        }
        head_.store(h, std::memory_order_release);
        return n;
    }

    bool isEmpty() const {
        return head_.load(std::memory_order_acquire) >= tail_.load(std::memory_order_acquire);
    }

    int capacity() const { return capacity_; }

    /** Bytes per slot; one cache line. */
    static constexpr std::size_t slotBytes() { return 64; }

    /** Distance of slot 0 from a 64-byte boundary. */
    std::size_t slotMisalignment() const {
        return reinterpret_cast<std::uintptr_t>(slots_.data()) % 64;
    }

private:
    struct alignas(64) Slot {
        std::int64_t orderId;
        std::int64_t refId;
        std::int64_t quantity;
        double price;
        std::int64_t timestampNanos;
        int symbolId;
        Side side;
        std::uint8_t type;
    };
    static_assert(sizeof(Slot) == 64, "one message, one cache line");

    // Java: Integer.highestOneBit(Math.max(2, requestedCapacity - 1)) * 2.
    static int roundCapacity(int requested) {
        unsigned v = static_cast<unsigned>(requested - 1 > 2 ? requested - 1 : 2);
        unsigned high = 1u << (31 - __builtin_clz(v));
        return static_cast<int>(high * 2);
    }

    const int capacity_;
    const int mask_;
    std::vector<Slot> slots_;

    // Padded sequences and per-side caches, one cache line each -- same
    // reasoning as TickRingBuffer: the trading thread stores cachedHead
    // and the venue thread cachedTail, and sharing a line with each other
    // or the slot-array fields would put false sharing back on the submit
    // path.
    alignas(64) std::atomic<std::int64_t> head_{0};
    alignas(64) std::atomic<std::int64_t> tail_{0};
    alignas(64) std::int64_t cachedHead_ = 0;
    alignas(64) std::int64_t cachedTail_ = 0;
    alignas(64) char endPad_[64] = {};
};

}  // namespace quantfinlib
