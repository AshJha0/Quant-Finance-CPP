// Port of com.quantfinlib.data.TickFileReader (Java): reader/replayer for
// QFLT tick files (see TickFileWriter). Replay is deterministic and
// allocation-free per tick after the symbol definitions -- record a live
// session once, then run strategy, latency, and microstructure
// experiments against identical real tick sequences.
#pragma once

#include <cstdint>
#include <string>

namespace quantfinlib {

class TickFileReader {
public:
    /** Replay callbacks: primitive tick data plus symbol definitions as encountered. */
    class ReplayHandler {
    public:
        virtual ~ReplayHandler() = default;

        virtual void onSymbol(int /*symbolId*/, const std::string& /*symbol*/) {}

        virtual void onTick(int symbolId, double price, double size,
                            std::int64_t timestampNanos) = 0;
    };

    TickFileReader() = delete;

    /**
     * Replays the file as fast as possible. Returns the number of ticks
     * delivered.
     * @throws std::runtime_error on a bad magic/version or a corrupt/truncated file
     */
    static std::int64_t replay(const std::string& path, ReplayHandler& handler);

    /**
     * Replays reproducing the recorded inter-tick gaps scaled by
     * speedMultiplier (2.0 = twice real time; individual gaps are capped
     * at 10s). For live-like feeds into the HFT bus.
     * @throws std::invalid_argument if speedMultiplier <= 0
     */
    static std::int64_t replayPaced(const std::string& path, ReplayHandler& handler,
                                    double speedMultiplier);
};

}  // namespace quantfinlib
