// Port of com.quantfinlib.trading.OrderRingBuffer (Java): zero-allocation
// single-producer / single-consumer ring buffer for order messages -- the
// order-entry mirror of the market-data TickRingBuffer: preallocated
// primitive slots, cache-line-padded (alignas(64)) sequences,
// acquire/release publication, and producer/consumer sequence caching.
#pragma once

#include <atomic>
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
              orderIds_(capacity_),
              symbolIds_(capacity_),
              sides_(capacity_),
              quantities_(capacity_),
              prices_(capacity_),
              timestamps_(capacity_) {}

    /** Producer side (trading thread). Returns false when full. */
    bool publish(std::int64_t orderId, int symbolId, Side side, std::int64_t quantity,
                 double price, std::int64_t timestampNanos) {
        std::int64_t t = tail_.load(std::memory_order_relaxed);   // producer-owned
        if (t - cachedHead_ >= capacity_) {
            cachedHead_ = head_.load(std::memory_order_acquire);
            if (t - cachedHead_ >= capacity_) {
                return false;
            }
        }
        int i = static_cast<int>(t & mask_);
        orderIds_[i] = orderId;
        symbolIds_[i] = symbolId;
        sides_[i] = static_cast<std::uint8_t>(side == Side::BUY ? 0 : 1);
        quantities_[i] = quantity;
        prices_[i] = price;
        timestamps_[i] = timestampNanos;
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
            sink(orderIds_[i], symbolIds_[i], sides_[i] == 0 ? Side::BUY : Side::SELL,
                 quantities_[i], prices_[i], timestamps_[i]);
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
    std::vector<std::int64_t> orderIds_;
    std::vector<int> symbolIds_;
    std::vector<std::uint8_t> sides_;
    std::vector<std::int64_t> quantities_;
    std::vector<double> prices_;
    std::vector<std::int64_t> timestamps_;

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
