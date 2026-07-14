// Port of com.quantfinlib.data.TickCapture (Java): records every tick
// flowing through an HftMarketDataBus into a QFLT tick file: attach once,
// trade/observe as usual, close to flush -- then replay the session
// deterministically with TickFileReader.
//
// Writing happens on the bus consumer thread through TickFileWriter's
// buffered stream (~28 bytes/tick), which is comfortably below the bus's
// throughput; a disk error surfaces as std::runtime_error on that thread.
// Latency note: a page-cache writeback or disk hiccup therefore stalls the
// consumer thread. Fine for capture-only or research sessions; when the
// same bus is TRADING, use AsyncTickCapture, which moves the file I/O to a
// dedicated writer thread behind a ring.
#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "quantfinlib/data/tick_file_writer.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"

namespace quantfinlib {

class TickCapture {
public:
    /** Creates a capture and subscribes it to every symbol on the bus. */
    static std::unique_ptr<TickCapture> attach(HftMarketDataBus& bus, const std::string& file);

    TickCapture(const TickCapture&) = delete;
    TickCapture& operator=(const TickCapture&) = delete;

    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos);

    std::int64_t ticksWritten() const { return writer_.tickCount(); }

    /** Flushes and closes the underlying file. Safe to call more than once. */
    void close() { writer_.close(); }

private:
    explicit TickCapture(HftMarketDataBus& bus, const std::string& file)
            : bus_(bus), writer_(file) {}

    HftMarketDataBus& bus_;
    TickFileWriter writer_;
};

}  // namespace quantfinlib
