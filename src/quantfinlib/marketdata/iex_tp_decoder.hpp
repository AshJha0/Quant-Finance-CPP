// IEX-TP segment and DEEP / DEEP+ message decoder, from the public
// specifications: IEX Transport v1.25, DEEP v1.08, DEEP+ v1.06. All fields are
// little endian. Prices are signed 8-byte counts of 0.0001; timestamps are
// nanoseconds since the POSIX epoch, UTC.
//
// A segment is walked in full before any message is delivered. A malformed
// segment delivers nothing and does not advance the sequence, so the next good
// segment reports the loss as a gap rather than the stream carrying on silently.
// Messages may grow at the end, as the specification allows; unknown types are
// counted and skipped.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quantfinlib {

struct IexMessage {
    char type = 0;               // the wire type byte
    std::uint8_t flags = 0;      // byte 1: event flags, sale flags, modify flags, status
    std::int64_t timestamp = 0;
    std::uint64_t symbol = 0;    // the 8 padded symbol bytes, as a map key
    std::uint32_t size = 0;
    std::int64_t price = 0;
    std::int64_t orderId = 0;    // DEEP+ only
    std::int64_t tradeId = 0;
};

class IexTpDecoder {
public:
    static constexpr std::uint16_t PROTOCOL_DEEP = 0x8004;
    static constexpr std::uint16_t PROTOCOL_DEEP_PLUS = 0x8005;
    static constexpr std::size_t HEADER_SIZE = 40;
    static constexpr std::size_t MAX_STREAMS = 16;

    struct Handler {
        virtual ~Handler() = default;
        virtual void onMessage(const IexMessage& message) = 0;
        /** Messages were lost: state built so far can no longer be trusted. */
        virtual void onGap(std::int64_t lostMessages) { (void)lostMessages; }
    };

    struct Stats {
        std::uint64_t segments = 0;
        std::uint64_t heartbeats = 0;
        std::uint64_t messages = 0;
        std::uint64_t badSegments = 0;    // header or framing could not be trusted
        std::uint64_t badMessages = 0;    // framed, but a field is short or invalid
        std::uint64_t unknownProtocol = 0;
        std::uint64_t unknownType = 0;
        std::uint64_t grownMessages = 0;
        std::uint64_t ignored = 0;        // known types with no use here: I, X, A
        std::uint64_t seqGaps = 0;
        std::uint64_t gapMessages = 0;
        std::uint64_t offsetMismatches = 0;
        std::uint64_t duplicateSegments = 0;
        std::uint64_t duplicateMessages = 0;
        std::uint64_t lateStartMessages = 0;
        std::uint64_t sendTimeRegressions = 0;
        std::uint64_t streams = 0;
        std::array<std::uint64_t, 256> byType{};
    };

    /** Decodes one UDP payload, calling the handler per message. */
    void onDatagram(const std::uint8_t* data, std::size_t size, Handler& handler);

    const Stats& stats() const { return stats_; }

private:
    struct Stream {
        std::uint16_t protocol;
        std::uint32_t channel;
        std::uint32_t session;
        std::int64_t nextSeq;
        std::int64_t nextOffset;
        std::int64_t lastSend;
    };

    void decodeMessage(const std::uint8_t* m, std::size_t length, std::uint16_t protocol,
                       Handler& handler);

    std::vector<Stream> streams_;
    Stats stats_;
};

}  // namespace quantfinlib
