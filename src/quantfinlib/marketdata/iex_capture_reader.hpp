// Streaming reader for the UDP payloads of a packet capture: classic pcap and
// pcapng. IEX HIST files are named .pcap.gz but hold pcapng, so both are
// needed. Decompression stays outside the library: feed it from `gzip -dc`.
//
// Every length comes from the file and is treated as hostile. A record that
// cannot be trusted stops the reader (failed() turns true), because after a
// bad length there is no way to find the next record boundary. Packets that
// are well framed but not wanted are counted and skipped.
#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace quantfinlib {

class IexCaptureReader {
public:
    /** Fills up to n bytes and returns how many; 0 means end of input. */
    using Source = std::function<std::size_t(std::uint8_t*, std::size_t)>;

    /** Largest record accepted; a jumbo frame is far below this. */
    static constexpr std::uint32_t MAX_RECORD = 1U << 18;

    struct Datagram {
        const std::uint8_t* data = nullptr;  // valid until the next call to next()
        std::size_t size = 0;
    };

    struct Stats {
        std::uint64_t packets = 0;
        std::uint64_t datagrams = 0;
        std::uint64_t notUdp = 0;
        std::uint64_t fragments = 0;
        std::uint64_t snapped = 0;
        std::uint64_t unknownLinkType = 0;
        std::uint64_t otherBlocks = 0;
        std::uint64_t truncated = 0;
        std::uint64_t badFormat = 0;
    };

    explicit IexCaptureReader(Source source);

    /** Next UDP payload; false at end of input or after a failure. */
    bool next(Datagram& out);

    bool failed() const { return failed_; }
    bool pcapng() const { return pcapng_; }
    const Stats& stats() const { return stats_; }

private:
    enum class Mode { Start, Classic, Ng, Done };

    bool ensure(std::size_t n);
    bool fail(std::uint64_t& counter);
    bool atCleanEnd(std::size_t n);
    bool readHeader();
    bool nextClassic(Datagram& out, bool& got);
    bool nextNg(Datagram& out, bool& got);
    void extract(const std::uint8_t* frame, std::size_t captured, std::uint32_t linkType,
                 Datagram& out, bool& got);
    std::uint32_t u32(const std::uint8_t* p) const;

    Source source_;
    std::vector<std::uint8_t> buffer_;
    std::size_t begin_ = 0;
    std::size_t end_ = 0;
    bool eof_ = false;
    Mode mode_ = Mode::Start;
    bool swapped_ = false;
    bool pcapng_ = false;
    bool failed_ = false;
    std::uint32_t classicLink_ = 0;
    std::vector<std::uint32_t> interfaces_;
    Stats stats_;
};

}  // namespace quantfinlib
