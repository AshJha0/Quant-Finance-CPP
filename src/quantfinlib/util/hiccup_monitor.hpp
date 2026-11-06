// Port of com.quantfinlib.util.HiccupMonitor (Java): jHiccup-style
// platform stall monitor -- a background thread repeatedly sleeps for a
// fixed resolution and records how much LONGER than requested each sleep
// took. Any excess is a "hiccup": OS scheduler preemption, timer
// coarseness, page faults -- exactly the pauses that corrupt latency
// percentiles without showing up in application code. (The Java original
// also caught GC/safepoint stalls; C++ has neither, which is the point
// of the port.)
//
// Run one alongside a latency benchmark: if the benchmark's p99.9 spikes
// while the hiccup monitor shows a matching stall, the platform (not your
// code) ate the tail. Allocation-free while sampling (LatencyRecorder
// histogram). Java's daemon thread becomes a joined thread: stop()/close()
// and the destructor join it.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>

#include "quantfinlib/util/latency_recorder.hpp"

namespace quantfinlib {

class HiccupMonitor {
public:
    /** Default 1 ms sampling resolution. */
    HiccupMonitor() : HiccupMonitor(1000000LL) {}

    explicit HiccupMonitor(std::int64_t resolutionNanos)
            : resolutionNanos_(resolutionNanos) {
        if (resolutionNanos < 100000) {
            throw std::invalid_argument(
                    "resolution below 100us measures spin noise, not hiccups");
        }
    }

    HiccupMonitor(const HiccupMonitor&) = delete;
    HiccupMonitor& operator=(const HiccupMonitor&) = delete;

    ~HiccupMonitor() { close(); }

    /** No-op while running; after stop() it joins the old thread and restarts. */
    HiccupMonitor& start() {
        if (running_.load(std::memory_order_acquire)) {
            return *this;
        }
        if (thread_.joinable()) {
            thread_.join();
        }
        running_.store(true, std::memory_order_release);
        thread_ = std::thread([this] { sampleLoop(); });
        return *this;
    }

    void stop() { running_.store(false, std::memory_order_release); }

    void close() {
        stop();
        if (thread_.joinable()) {
            thread_.join();
        }
    }

    /**
     * The hiccup histogram: excess-over-requested sleep time, in nanos. The
     * recorder is single-writer, so read it only after close(). samples(),
     * maxHiccupNanos() and summary() are safe while the monitor runs.
     */
    const LatencyRecorder& recorder() const { return recorder_; }

    std::int64_t samples() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return recorder_.count();
    }

    /** Worst platform stall observed, in nanoseconds. */
    std::int64_t maxHiccupNanos() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return recorder_.max();
    }

    std::string summary() const {
        char head[64];
        std::snprintf(head, sizeof(head), "platform hiccups (res=%gms): ",
                      static_cast<double>(resolutionNanos_) / 1000000.0);
        std::lock_guard<std::mutex> lock(mutex_);
        return std::string(head) + recorder_.summary();
    }

private:
    void sampleLoop() {
        using clock = std::chrono::steady_clock;
        while (running_.load(std::memory_order_acquire)) {
            auto before = clock::now();
            std::this_thread::sleep_for(std::chrono::nanoseconds(resolutionNanos_));
            std::int64_t excess =
                    std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now() - before)
                            .count()
                    - resolutionNanos_;
            std::lock_guard<std::mutex> lock(mutex_);   // once per sleep, off any hot path
            recorder_.record(excess > 0 ? excess : 0);
        }
    }

    const std::int64_t resolutionNanos_;
    mutable std::mutex mutex_;
    LatencyRecorder recorder_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

}  // namespace quantfinlib
