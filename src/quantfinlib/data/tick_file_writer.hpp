// Port of com.quantfinlib.data.TickFileWriter (Java): writer for the QFLT
// binary tick format -- compact capture of live tick streams for
// deterministic replay (28 bytes per tick, buffered sequential writes).
//
// Format: magic "QFLT" + version byte, then framed records: type 1 =
// symbol definition (int32 id, uint16-length UTF-8 name), type 0 = tick
// (int32 symbolId, double price, double size, int64 timestampNanos).
// Symbol definitions may appear anywhere before the first tick that
// references them, so symbols can be added mid-capture.
//
// Wire note: Java's DataOutputStream is BIG-ENDIAN (unlike the
// little-endian sbe:: flyweights), so every multi-byte field here is
// written most-significant-byte first -- that is the QFLT wire truth,
// ported byte for byte.
#pragma once

#include <cstdint>
#include <fstream>
#include <string>
#include <vector>

namespace quantfinlib {

class TickFileWriter {
public:
    static constexpr std::int32_t MAGIC = 0x51464C54;  // "QFLT"
    static constexpr std::uint8_t VERSION = 1;
    static constexpr std::uint8_t TYPE_TICK = 0;
    static constexpr std::uint8_t TYPE_SYMBOL = 1;

    /** @throws std::runtime_error if the file cannot be opened for writing. */
    explicit TickFileWriter(const std::string& path);
    ~TickFileWriter();

    TickFileWriter(const TickFileWriter&) = delete;
    TickFileWriter& operator=(const TickFileWriter&) = delete;

    /** Registers a symbol id (idempotent; must precede its first tick). */
    void defineSymbol(int symbolId, const std::string& symbol);

    /** @throws std::runtime_error if symbolId was not defineSymbol()'d first. */
    void write(int symbolId, double price, double size, std::int64_t timestampNanos);

    std::int64_t tickCount() const { return tickCount_; }

    /** Flushes and closes the underlying file. Safe to call more than once. */
    void close();

private:
    std::ofstream out_;
    std::vector<bool> defined_;
    std::int64_t tickCount_ = 0;
};

}  // namespace quantfinlib
