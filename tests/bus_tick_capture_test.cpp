// Port of the Java TickCapture / AsyncTickCapture behavior pins: a capture
// attached to a live bus records every tick to a QFLT file byte-faithfully
// (replayed back via TickFileReader, symbol names and values must match
// exactly), and the async capture's honest drop accounting -- every tick
// handed to onTick() is EITHER written or counted dropped, never silently
// lost (droppedTicks() + ticksWritten() == total calls, always).
#include "quantfinlib/data/async_tick_capture.hpp"
#include "quantfinlib/data/tick_capture.hpp"
#include "quantfinlib/data/tick_file_reader.hpp"
#include "quantfinlib/marketdata/hft_market_data_bus.hpp"

#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "qtest.hpp"

using quantfinlib::AsyncTickCapture;
using quantfinlib::HftMarketDataBus;
using quantfinlib::TickCapture;
using quantfinlib::TickFileReader;

namespace {

std::string tempPath(const char* name) {
    return std::string(std::getenv("TEMP") ? std::getenv("TEMP") : ".") + "/" + name;
}

struct RecordingHandler : TickFileReader::ReplayHandler {
    std::vector<std::pair<int, std::string>> symbols;
    std::vector<std::tuple<int, double, double, std::int64_t>> ticks;

    void onSymbol(int symbolId, const std::string& symbol) override {
        symbols.emplace_back(symbolId, symbol);
    }
    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos) override {
        ticks.emplace_back(symbolId, price, size, timestampNanos);
    }
};

}  // namespace

TEST(tickCaptureRecordsBusTicksReplayableByteFaithfully) {
    std::string path = tempPath("qfl_tick_capture_test.qflt");
    HftMarketDataBus bus(64, 4, false);
    int eur = bus.registerSymbol("EURUSD");
    int usd = bus.registerSymbol("USDJPY");

    auto capture = TickCapture::attach(bus, path);
    bus.publish(eur, 1.1000, 1e6, 100);
    bus.publish(usd, 150.25, 2e6, 200);
    bus.publish(eur, 1.1005, 1.5e6, 300);

    bus.start();
    for (int i = 0; i < 1000 && bus.processedCount() < 3; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    bus.stop();
    EXPECT_TRUE(capture->ticksWritten() == 3);
    capture->close();

    RecordingHandler handler;
    std::int64_t n = TickFileReader::replay(path, handler);
    EXPECT_TRUE(n == 3);
    EXPECT_TRUE(handler.ticks.size() == 3);
    EXPECT_TRUE(std::get<0>(handler.ticks[0]) == eur);
    EXPECT_NEAR(std::get<1>(handler.ticks[0]), 1.1000, 1e-12);
    EXPECT_TRUE(std::get<3>(handler.ticks[0]) == 100);
    EXPECT_TRUE(std::get<0>(handler.ticks[1]) == usd);
    EXPECT_NEAR(std::get<1>(handler.ticks[1]), 150.25, 1e-12);
    EXPECT_TRUE(std::get<0>(handler.ticks[2]) == eur);
    EXPECT_NEAR(std::get<1>(handler.ticks[2]), 1.1005, 1e-12);

    bool sawEur = false, sawUsd = false;
    for (auto& [id, name] : handler.symbols) {
        if (id == eur) sawEur = (name == "EURUSD");
        if (id == usd) sawUsd = (name == "USDJPY");
    }
    EXPECT_TRUE(sawEur);
    EXPECT_TRUE(sawUsd);

    std::remove(path.c_str());
}

TEST(asyncTickCaptureNeverSilentlyLosesATick) {
    std::string path = tempPath("qfl_async_tick_capture_test.qflt");
    HftMarketDataBus bus(64, 2, false);
    int id = bus.registerSymbol("X");

    // Tiny ring (rounds to 4): the drain thread will very likely fall
    // behind this tight-loop producer at least sometimes, exercising the
    // drop path -- but regardless of actual timing, the conservation
    // identity below must hold exactly.
    auto capture = AsyncTickCapture::attach(bus, path, 1);
    constexpr int N = 50000;
    for (int i = 0; i < N; i++) {
        capture->onTick(id, 100.0 + i * 0.0001, 1.0, i);
    }
    capture->close();  // final drain: nothing left in the ring after this

    EXPECT_TRUE(capture->droppedTicks() + capture->ticksWritten() == N);
    std::remove(path.c_str());
}

QTEST_MAIN()
