#include "quantfinlib/data/async_tick_capture.hpp"

#include <chrono>

namespace quantfinlib {

std::unique_ptr<AsyncTickCapture> AsyncTickCapture::attach(HftMarketDataBus& bus,
                                                             const std::string& file,
                                                             int ringCapacity) {
    std::unique_ptr<AsyncTickCapture> capture(new AsyncTickCapture(bus, file, ringCapacity));
    AsyncTickCapture* raw = capture.get();
    bus.subscribeAll([raw](int symbolId, double price, double size, std::int64_t timestampNanos) {
        raw->onTick(symbolId, price, size, timestampNanos);
    });
    capture->drainThread_ = std::thread([raw] { raw->drainLoop(); });
    return capture;
}

void AsyncTickCapture::onTick(int symbolId, double price, double size,
                               std::int64_t timestampNanos) {
    if (!ring_.publish(symbolId, price, size, timestampNanos)) {
        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

void AsyncTickCapture::drainLoop() {
    auto sink = [this](int symbolId, double price, double size, std::int64_t timestampNanos) {
        writer_.defineSymbol(symbolId, bus_.symbol(symbolId));
        writer_.write(symbolId, price, size, timestampNanos);
    };
    while (running_.load(std::memory_order_acquire)) {
        if (ring_.drainTo(sink, 4096) == 0) {
            // Idle: yield rather than spin -- the writer thread has no
            // latency budget, only the consumer thread does.
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    }
    // Final drain so close() loses nothing that made it into the ring.
    while (ring_.drainTo(sink, 4096) > 0) {
        // keep draining
    }
}

void AsyncTickCapture::close() {
    running_.store(false, std::memory_order_release);
    if (drainThread_.joinable()) {
        drainThread_.join();
    }
    writer_.close();
}

}  // namespace quantfinlib
