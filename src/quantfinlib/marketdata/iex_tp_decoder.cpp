#include "quantfinlib/marketdata/iex_tp_decoder.hpp"

#include <cstring>
#include <limits>

namespace quantfinlib {

namespace {

std::uint16_t u16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0] | p[1] << 8);
}

std::uint32_t u32(const std::uint8_t* p) {
    std::uint32_t v = 0;
    for (int i = 3; i >= 0; --i) {
        v = v << 8 | p[i];
    }
    return v;
}

std::int64_t i64(const std::uint8_t* p) {
    std::uint64_t v = 0;
    for (int i = 7; i >= 0; --i) {
        v = v << 8 | p[i];
    }
    return static_cast<std::int64_t>(v);
}

// Exact length of each type, 0 when the protocol does not define it, -1 when
// the type is known but nothing here uses it.
int expectedLength(std::uint8_t type, std::uint16_t protocol) {
    bool deep = protocol == IexTpDecoder::PROTOCOL_DEEP;
    switch (type) {
        case 'S': return 10;
        case 'D': return 31;
        case 'H': return 22;
        case 'O': case 'E': return 18;
        case 'P': return 19;
        case 'T': case 'B': return 38;
        case 'I': return -1;
        case '8': case '5': return deep ? 30 : 0;
        case 'X': case 'A': return deep ? -1 : 0;
        case 'a': case 'M': return deep ? 0 : 38;
        case 'R': return deep ? 0 : 26;
        case 'L': return deep ? 0 : 46;
        case 'C': return deep ? 0 : 18;
        default: return 0;
    }
}

}  // namespace

void IexTpDecoder::onDatagram(const std::uint8_t* data, std::size_t size, Handler& handler) {
    ++stats_.segments;
    if (size < HEADER_SIZE || data[0] != 1) {
        ++stats_.badSegments;
        return;
    }
    std::uint16_t protocol = u16(data + 2);
    if (protocol != PROTOCOL_DEEP && protocol != PROTOCOL_DEEP_PLUS) {
        ++stats_.unknownProtocol;
        return;
    }
    std::uint32_t channel = u32(data + 4);
    std::uint32_t session = u32(data + 8);
    std::size_t payload = u16(data + 12);
    std::int64_t count = u16(data + 14);
    std::int64_t offset = i64(data + 16);
    std::int64_t seq = i64(data + 24);
    std::int64_t send = i64(data + 32);
    constexpr std::int64_t kLimit = std::numeric_limits<std::int64_t>::max() - (1 << 17);
    if (size - HEADER_SIZE < payload || seq < 0 || seq > kLimit || offset < 0 || offset > kLimit ||
        (count == 0) != (payload == 0)) {
        ++stats_.badSegments;
        return;
    }

    // Walk the blocks first: nothing is delivered from a segment that lies.
    const std::uint8_t* body = data + HEADER_SIZE;
    std::size_t at = 0;
    std::int64_t blocks = 0;
    while (at < payload) {
        if (payload - at < 2) {
            ++stats_.badSegments;
            return;
        }
        std::size_t length = u16(body + at);
        if (length == 0 || payload - at - 2 < length) {
            ++stats_.badSegments;
            return;
        }
        at += 2 + length;
        ++blocks;
    }
    if (blocks != count) {
        ++stats_.badSegments;
        return;
    }

    Stream* stream = nullptr;
    for (Stream& s : streams_) {
        if (s.protocol == protocol && s.channel == channel && s.session == session) {
            stream = &s;
            break;
        }
    }
    std::int64_t followingSeq = seq + count;
    std::int64_t followingOffset = offset + static_cast<std::int64_t>(payload);
    std::int64_t skip = 0;
    if (stream == nullptr) {
        if (streams_.size() >= MAX_STREAMS) {
            ++stats_.badSegments;
            return;
        }
        streams_.push_back({protocol, channel, session, followingSeq, followingOffset, send});
        ++stats_.streams;
        if (seq > 1) {
            stats_.lateStartMessages += static_cast<std::uint64_t>(seq - 1);
            handler.onGap(seq - 1);
        }
    } else {
        // A heartbeat carries the next sequence, so one at nextSeq is not a duplicate.
        if (seq < stream->nextSeq && followingSeq <= stream->nextSeq) {
            ++stats_.duplicateSegments;
            return;
        }
        if (send < stream->lastSend) {
            ++stats_.sendTimeRegressions;
        } else {
            stream->lastSend = send;
        }
        if (seq < stream->nextSeq) {
            skip = stream->nextSeq - seq;
            stats_.duplicateMessages += static_cast<std::uint64_t>(skip);
        } else if (seq > stream->nextSeq) {
            std::int64_t lost = seq - stream->nextSeq;
            ++stats_.seqGaps;
            stats_.gapMessages += static_cast<std::uint64_t>(lost);
            handler.onGap(lost);
        } else if (offset != stream->nextOffset) {
            ++stats_.offsetMismatches;
        }
        stream->nextSeq = followingSeq;
        stream->nextOffset = followingOffset;
    }

    if (count == 0) {
        ++stats_.heartbeats;
        return;
    }
    at = 0;
    for (std::int64_t i = 0; i < count; ++i) {
        std::size_t length = u16(body + at);
        if (i >= skip) {
            decodeMessage(body + at + 2, length, protocol, handler);
        }
        at += 2 + length;
    }
}

void IexTpDecoder::decodeMessage(const std::uint8_t* m, std::size_t length,
                                 std::uint16_t protocol, Handler& handler) {
    ++stats_.messages;
    std::uint8_t type = m[0];
    ++stats_.byType[type];
    int expected = expectedLength(type, protocol);
    if (expected == 0) {
        ++stats_.unknownType;
        return;
    }
    if (expected < 0) {
        ++stats_.ignored;
        return;
    }
    if (length < static_cast<std::size_t>(expected)) {
        ++stats_.badMessages;
        return;
    }
    if (length > static_cast<std::size_t>(expected)) {
        ++stats_.grownMessages;
    }
    IexMessage out;
    out.type = static_cast<char>(type);
    out.flags = m[1];
    out.timestamp = i64(m + 2);
    if (out.timestamp < 0) {
        ++stats_.badMessages;
        return;
    }
    if (type != 'S') {
        std::memcpy(&out.symbol, m + 10, 8);
    }
    bool valid = true;
    switch (type) {
        case '8': case '5':
            out.size = u32(m + 18);
            out.price = i64(m + 22);
            valid = out.price >= 1;  // size 0 removes the level, but the price is still real
            break;
        case 'T': case 'B':
            out.size = u32(m + 18);
            out.price = i64(m + 22);
            out.tradeId = i64(m + 30);
            valid = out.price >= 1 && out.size != 0;
            break;
        case 'a': case 'M': case 'L':
            out.orderId = i64(m + 18);
            out.size = u32(m + 26);
            out.price = i64(m + 30);
            if (type == 'L') {
                out.tradeId = i64(m + 38);
            }
            valid = out.orderId >= 1 && out.size != 0 && out.price >= 1 &&
                    (type != 'a' || m[1] == '8' || m[1] == '5');
            break;
        case 'R':
            out.orderId = i64(m + 18);
            valid = out.orderId >= 1;
            break;
        default:
            break;
    }
    if (!valid) {
        ++stats_.badMessages;
        return;
    }
    handler.onMessage(out);
}

}  // namespace quantfinlib
