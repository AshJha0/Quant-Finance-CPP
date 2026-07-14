#include "quantfinlib/fix/fix_decoder.hpp"

#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace quantfinlib {

void FixDecoder::feed(const std::uint8_t* data, std::size_t offset, std::size_t count) {
    if (length_ + count > buffer_.size()) {
        buffer_.resize(std::max(buffer_.size() * 2, length_ + count));
    }
    std::copy(data + offset, data + offset + count, buffer_.begin() + static_cast<long>(length_));
    length_ += count;
}

std::size_t FixDecoder::indexOf(std::uint8_t c, std::size_t from) const {
    for (std::size_t i = from; i < length_; ++i) {
        if (buffer_[i] == c) {
            return i;
        }
    }
    return static_cast<std::size_t>(-1);
}

std::optional<FixMessage> FixDecoder::poll() {
    constexpr std::size_t npos = static_cast<std::size_t>(-1);
    if (length_ < 20) {
        return std::nullopt;
    }
    // Locate "9=<digits>SOH" following the BeginString field.
    std::size_t firstSoh = indexOf(static_cast<std::uint8_t>(FixMessage::SOH), 0);
    if (firstSoh == npos) {
        return std::nullopt;
    }
    if (buffer_[firstSoh + 1] != '9' || buffer_[firstSoh + 2] != '=') {
        throw std::runtime_error("stream corrupt: BodyLength not after BeginString");
    }
    std::size_t lenEnd = indexOf(static_cast<std::uint8_t>(FixMessage::SOH), firstSoh + 3);
    if (lenEnd == npos) {
        return std::nullopt;
    }
    if (lenEnd == firstSoh + 3) {
        throw std::runtime_error("stream corrupt: empty BodyLength");
    }
    std::int64_t bodyLen = 0;
    for (std::size_t i = firstSoh + 3; i < lenEnd; ++i) {
        int digit = buffer_[i] - '0';
        // A single corrupted byte here would silently inflate the expected
        // frame length and the framer would wait FOREVER for bytes that
        // never come -- a zombie session swallowing every later valid
        // message. Corruption must fail loudly so the session layer
        // disconnects.
        if (digit < 0 || digit > 9) {
            char hex[8];
            std::snprintf(hex, sizeof(hex), "%02x", buffer_[i] & 0xFF);
            throw std::runtime_error(std::string("stream corrupt: BodyLength contains 0x") + hex);
        }
        bodyLen = bodyLen * 10 + digit;
        // Checked INSIDE the loop: ten corrupt-but-numeric digits would
        // wrap the value and could sneak under a post-loop cap.
        if (bodyLen > (1 << 20)) {
            throw std::runtime_error("stream corrupt: BodyLength " + std::to_string(bodyLen)
                                      + " is not a FIX message");
        }
    }
    std::size_t total = lenEnd + 1 + static_cast<std::size_t>(bodyLen) + 7;  // "10=xxx" + SOH
    if (length_ < total) {
        return std::nullopt;
    }
    std::vector<std::uint8_t> message(buffer_.begin(), buffer_.begin() + static_cast<long>(total));
    std::copy(buffer_.begin() + static_cast<long>(total), buffer_.begin() + static_cast<long>(length_),
               buffer_.begin());
    length_ -= total;
    return FixMessage::parse(message);
}

}  // namespace quantfinlib
