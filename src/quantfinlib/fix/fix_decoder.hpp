// Port of com.quantfinlib.fix.FixDecoder (Java, package-private): incremental
// stream framer for FIX messages -- feed raw socket bytes in any
// fragmentation, poll complete validated FixMessages out. Framing uses
// BodyLength(9), so message boundaries are exact regardless of TCP
// segmentation. Exposed (not package-private, C++ has no equivalent) but
// documented as an internal building block used by FixSession.
#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "quantfinlib/fix/fix_message.hpp"

namespace quantfinlib {

class FixDecoder {
public:
    /** Largest BodyLength(9) accepted once a session is logged on. */
    static constexpr std::int64_t MAX_BODY_LENGTH = 1 << 20;

    /** Throws std::runtime_error if the stream exceeds the buffer limit: poll between feeds. */
    void feed(const std::uint8_t* data, std::size_t offset, std::size_t count);

    /** Lowers the frame limit, e.g. before Logon. Clamped to MAX_BODY_LENGTH. */
    void setMaxBodyLength(std::int64_t maxBodyLength);

    /** Next complete message, or std::nullopt if more bytes are needed. Throws std::runtime_error
     *  (stream corrupt: framing is lost, the caller must disconnect) or std::invalid_argument (one
     *  garbled frame, already consumed: the stream is still in sync). */
    std::optional<FixMessage> poll();

private:
    std::size_t indexOf(std::uint8_t c, std::size_t from) const;

    std::vector<std::uint8_t> buffer_ = std::vector<std::uint8_t>(8192);
    std::size_t length_ = 0;
    std::int64_t maxBodyLength_ = MAX_BODY_LENGTH;
};

}  // namespace quantfinlib
