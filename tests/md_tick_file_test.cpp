// Port of TickFileTest (Java): QFLT write -> read round trip with
// mid-stream symbol definitions, writer rejection of undefined symbols,
// reader rejection of a corrupt-magic file, and paced replay delivering
// every tick with a positive-speed-multiplier guard. The Java
// HftMarketDataBus capture test (capturesLiveBusTrafficForExactReplay)
// needs the live bus/ring infra explicitly out of Phase 4A's scope and is
// not ported.
#include "quantfinlib/data/tick_file_reader.hpp"
#include "quantfinlib/data/tick_file_writer.hpp"

#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

#include "qtest.hpp"

using quantfinlib::TickFileReader;
using quantfinlib::TickFileWriter;

namespace {

struct Tick {
    int symbolId;
    double price;
    double size;
    std::int64_t ts;

    bool operator==(const Tick& o) const {
        return symbolId == o.symbolId && price == o.price && size == o.size && ts == o.ts;
    }
};

class Collector : public TickFileReader::ReplayHandler {
public:
    std::vector<std::pair<int, std::string>> symbols;
    std::vector<Tick> ticks;

    void onSymbol(int symbolId, const std::string& symbol) override {
        symbols.push_back({symbolId, symbol});
    }

    void onTick(int symbolId, double price, double size, std::int64_t timestampNanos) override {
        ticks.push_back({symbolId, price, size, timestampNanos});
    }

    const std::string* symbolFor(int id) const {
        for (auto& p : symbols) {
            if (p.first == id) {
                return &p.second;
            }
        }
        return nullptr;
    }
};

class NullHandler : public TickFileReader::ReplayHandler {
public:
    void onTick(int, double, double, std::int64_t) override {}
};

// Scratch file path inside the build directory (no external temp dir dependency).
std::string scratchPath(const char* name) {
    return std::string("md_tick_file_scratch_") + name + ".qflt";
}

}  // namespace

TEST(roundTripsTicksAndMidStreamSymbolDefinitions) {
    std::string file = scratchPath("roundtrip");
    {
        TickFileWriter w(file);
        w.defineSymbol(0, "EURUSD");
        w.write(0, 1.0850, 1000000, 100);
        w.write(0, 1.0851, 500000, 200);
        w.defineSymbol(1, "GBPUSD");           // defined mid-capture
        w.write(1, 1.2700, 250000, 300);
        w.write(0, 1.0852, 750000, 400);
        EXPECT_TRUE(w.tickCount() == 4);
    }

    Collector c;
    std::int64_t ticks = TickFileReader::replay(file, c);

    EXPECT_TRUE(ticks == 4);
    EXPECT_TRUE(*c.symbolFor(0) == "EURUSD");
    EXPECT_TRUE(*c.symbolFor(1) == "GBPUSD");
    EXPECT_TRUE((c.ticks.front() == Tick{0, 1.0850, 1000000, 100}));
    EXPECT_TRUE((c.ticks[2] == Tick{1, 1.2700, 250000, 300}));
    EXPECT_TRUE((c.ticks.back() == Tick{0, 1.0852, 750000, 400}));
    std::remove(file.c_str());
}

TEST(writerRejectsUndefinedSymbols) {
    std::string file = scratchPath("undefined");
    {
        TickFileWriter w(file);
        EXPECT_THROWS(w.write(5, 1.0, 1, 1));
    }
    std::remove(file.c_str());
}

TEST(readerRejectsCorruptFiles) {
    std::string file = scratchPath("junk");
    {
        std::ofstream out(file, std::ios::binary);
        unsigned char junk[] = {1, 2, 3, 4, 5, 6, 7, 8};
        out.write(reinterpret_cast<const char*>(junk), sizeof(junk));
    }
    NullHandler h;
    EXPECT_THROWS(TickFileReader::replay(file, h));
    std::remove(file.c_str());
}

TEST(pacedReplayDeliversEverythingScaled) {
    std::string file = scratchPath("paced");
    {
        TickFileWriter w(file);
        w.defineSymbol(0, "X");
        for (int i = 0; i < 5; i++) {
            w.write(0, 100 + i, 1, static_cast<std::int64_t>(i) * 10000000LL);  // 10ms gaps
        }
    }
    NullHandler h;
    auto t0 = std::chrono::steady_clock::now();
    std::int64_t ticks = TickFileReader::replayPaced(file, h, 100);   // 100x speed
    auto elapsed = std::chrono::steady_clock::now() - t0;
    EXPECT_TRUE(ticks == 5);
    EXPECT_TRUE(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() < 500);
    EXPECT_THROWS(TickFileReader::replayPaced(file, h, 0));
    std::remove(file.c_str());
}

QTEST_MAIN()
