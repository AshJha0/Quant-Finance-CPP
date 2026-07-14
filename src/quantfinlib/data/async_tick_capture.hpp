// Port of com.quantfinlib.data.AsyncTickCapture (Java): tick capture with
// the file I/O taken OFF the bus consumer thread -- the hot-lane variant of
// TickCapture. The consumer thread only publishes into a private
// TickRingBuffer; a dedicated writer thread drains it into the QFLT file. A
// page-cache writeback, AV scan or disk hiccup then stalls the *writer*,
// never the trading loop.
//
// Backpressure policy: when the ring is full (writer stalled longer than
// the buffer absorbs), ticks are dropped from the capture -- counted in
// droppedTicks(), never blocking the consumer. Size the ring for the worst
// stall you tolerate: capacity 1M ~= 28 MB ~= several seconds of a busy
// feed.
#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

#include "quantfinlib/data/tick_file_writer.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"
#include "quantfinlib/util/tick_ring_buffer.hpp"

namespace quantfinlib {

class AsyncTickCapture {
public:
    /** Creates the capture, subscribes it to every symbol, starts the writer. */
    static std::unique_ptr<AsyncTickCapture> attach(HftMarketDataBus& bus, const std::string& file,
                                                      int ringCapacity);

    ~AsyncTickCapture() { close(); }

    AsyncTickCapture(const AsyncTickCapture&) = delete;
    AsyncTickCapture& operator=(const AsyncTickCapture&) = delete;

    /** The hot path: one ring publish; a full ring drops (counted), never blocks. */
    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos);

    /** Ticks dropped because the ring was full (writer stalled) -- monitor this. */
    std::int64_t droppedTicks() const { return dropped_.load(std::memory_order_acquire); }

    std::int64_t ticksWritten() const { return writer_.tickCount(); }

    /** Stops the writer (draining what remains) and closes the file. */
    void close();

private:
    AsyncTickCapture(HftMarketDataBus& bus, const std::string& file, int ringCapacity)
            : ring_(ringCapacity), writer_(file), bus_(bus) {}

    void drainLoop();

    TickRingBuffer ring_;
    TickFileWriter writer_;
    HftMarketDataBus& bus_;
    std::thread drainThread_;
    std::atomic<std::int64_t> dropped_{0};
    std::atomic<bool> running_{true};
};

}  // namespace quantfinlib
