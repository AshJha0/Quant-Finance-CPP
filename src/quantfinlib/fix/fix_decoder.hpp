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
    void feed(const std::uint8_t* data, std::size_t offset, std::size_t count);

    /** Next complete message, or std::nullopt if more bytes are needed. Throws std::runtime_error
     *  (stream corrupt) -- the caller must treat that as fatal and disconnect. */
    std::optional<FixMessage> poll();

private:
    std::size_t indexOf(std::uint8_t c, std::size_t from) const;

    std::vector<std::uint8_t> buffer_ = std::vector<std::uint8_t>(8192);
    std::size_t length_ = 0;
};

}  // namespace quantfinlib
