#include "quantfinlib/marketdata/iex_capture_reader.hpp"

#include <cstring>
#include <utility>

namespace quantfinlib {

namespace {

constexpr std::uint32_t kSectionHeader = 0x0A0D0D0A;
constexpr std::uint32_t kByteOrderMagic = 0x1A2B3C4D;
constexpr std::uint32_t kInterfaceBlock = 1;
constexpr std::uint32_t kEnhancedPacket = 6;
constexpr std::size_t kMaxInterfaces = 64;
constexpr std::uint32_t kLinkEthernet = 1;
constexpr std::uint32_t kLinkRaw = 101;
constexpr std::uint32_t kLinkRawV4 = 228;

std::uint32_t le32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

std::uint32_t be16(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) << 8 | p[1];
}

}  // namespace

IexCaptureReader::IexCaptureReader(Source source)
    : source_(std::move(source)), buffer_(1U << 22) {}

std::uint32_t IexCaptureReader::u32(const std::uint8_t* p) const {
    std::uint32_t v = le32(p);
    if (swapped_) {
        v = (v >> 24) | ((v >> 8) & 0xFF00) | ((v << 8) & 0xFF0000) | (v << 24);
    }
    return v;
}

bool IexCaptureReader::fail(std::uint64_t& counter) {
    ++counter;
    failed_ = true;
    mode_ = Mode::Done;
    return false;
}

// True when n bytes are buffered at begin_. n never exceeds the buffer: every
// caller has bounded it by MAX_RECORD plus a small header.
bool IexCaptureReader::ensure(std::size_t n) {
    while (end_ - begin_ < n) {
        if (eof_) {
            return false;
        }
        if (begin_ != 0) {
            std::memmove(buffer_.data(), buffer_.data() + begin_, end_ - begin_);
            end_ -= begin_;
            begin_ = 0;
        }
        std::size_t got = source_(buffer_.data() + end_, buffer_.size() - end_);
        if (got == 0) {
            eof_ = true;
        }
        end_ += got;
    }
    return true;
}

// After ensure(n) failed: end of input on a record boundary is a clean end,
// anything else is a cut file. Returns false either way.
bool IexCaptureReader::atCleanEnd(std::size_t) {
    if (end_ == begin_) {
        mode_ = Mode::Done;
        return false;
    }
    return fail(stats_.truncated);
}

bool IexCaptureReader::readHeader() {
    if (!ensure(4)) {
        return atCleanEnd(4);
    }
    std::uint32_t magic = le32(buffer_.data() + begin_);
    if (magic == kSectionHeader) {
        pcapng_ = true;
        mode_ = Mode::Ng;
        return true;
    }
    if (magic == 0xA1B2C3D4 || magic == 0xA1B23C4D) {
        swapped_ = false;
    } else if (magic == 0xD4C3B2A1 || magic == 0x4D3CB2A1) {
        swapped_ = true;
    } else {
        return fail(stats_.badFormat);
    }
    if (!ensure(24)) {
        return fail(stats_.truncated);
    }
    classicLink_ = u32(buffer_.data() + begin_ + 20) & 0x0FFFFFFF;
    begin_ += 24;
    mode_ = Mode::Classic;
    return true;
}

bool IexCaptureReader::nextClassic(Datagram& out, bool& got) {
    if (!ensure(16)) {
        return atCleanEnd(16);
    }
    std::uint32_t captured = u32(buffer_.data() + begin_ + 8);
    if (captured > MAX_RECORD) {
        return fail(stats_.badFormat);
    }
    if (!ensure(16 + static_cast<std::size_t>(captured))) {
        return fail(stats_.truncated);
    }
    const std::uint8_t* frame = buffer_.data() + begin_ + 16;
    begin_ += 16 + static_cast<std::size_t>(captured);
    extract(frame, captured, classicLink_, out, got);
    return true;
}

bool IexCaptureReader::nextNg(Datagram& out, bool& got) {
    if (!ensure(12)) {
        return atCleanEnd(12);
    }
    const std::uint8_t* block = buffer_.data() + begin_;
    std::uint32_t type = le32(block);
    if (type == kSectionHeader && le32(block + 8) != kByteOrderMagic) {
        return fail(stats_.badFormat);  // big-endian sections are not supported
    }
    std::uint32_t total = le32(block + 4);
    if (total < 12 || total % 4 != 0 || total > MAX_RECORD + 64) {
        return fail(stats_.badFormat);
    }
    if (!ensure(total)) {
        return fail(stats_.truncated);
    }
    block = buffer_.data() + begin_;  // ensure() may have moved the bytes
    if (le32(block + total - 4) != total) {
        return fail(stats_.badFormat);
    }
    begin_ += total;
    if (type == kSectionHeader) {
        interfaces_.clear();
    } else if (type == kInterfaceBlock) {
        if (total < 20 || interfaces_.size() >= kMaxInterfaces) {
            return fail(stats_.badFormat);
        }
        interfaces_.push_back(static_cast<std::uint32_t>(block[8]) |
                              static_cast<std::uint32_t>(block[9]) << 8);
    } else if (type == kEnhancedPacket) {
        if (total < 32) {
            return fail(stats_.badFormat);
        }
        std::uint32_t iface = le32(block + 8);
        std::uint32_t captured = le32(block + 20);
        if (iface >= interfaces_.size() || captured > total - 32) {
            return fail(stats_.badFormat);
        }
        extract(block + 28, captured, interfaces_[iface], out, got);
    } else {
        ++stats_.otherBlocks;
    }
    return true;
}

void IexCaptureReader::extract(const std::uint8_t* frame, std::size_t captured,
                               std::uint32_t linkType, Datagram& out, bool& got) {
    ++stats_.packets;
    std::size_t at = 0;
    std::uint32_t etherType = 0x0800;
    if (linkType == kLinkEthernet) {
        if (captured < 14) {
            ++stats_.snapped;
            return;
        }
        etherType = be16(frame + 12);
        at = 14;
        while (etherType == 0x8100 || etherType == 0x88A8 || etherType == 0x9100) {
            if (captured < at + 4) {
                ++stats_.snapped;
                return;
            }
            etherType = be16(frame + at + 2);
            at += 4;
        }
    } else if (linkType != kLinkRaw && linkType != kLinkRawV4) {
        ++stats_.unknownLinkType;
        return;
    }
    if (etherType != 0x0800) {
        ++stats_.notUdp;  // HIST captures are IPv4
        return;
    }
    if (captured < at + 20) {
        ++stats_.snapped;
        return;
    }
    const std::uint8_t* ip = frame + at;
    std::size_t ipHeader = static_cast<std::size_t>(ip[0] & 0x0F) * 4;
    if ((ip[0] >> 4) != 4 || ipHeader < 20 || ip[9] != 17) {
        ++stats_.notUdp;
        return;
    }
    if ((be16(ip + 6) & 0x3FFF) != 0) {  // more-fragments flag or a fragment offset
        ++stats_.fragments;
        return;
    }
    if (captured < at + ipHeader + 8) {
        ++stats_.snapped;
        return;
    }
    const std::uint8_t* udp = ip + ipHeader;
    std::size_t udpLength = be16(udp + 4);
    if (udpLength < 8 || captured < at + ipHeader + udpLength) {
        ++stats_.snapped;
        return;
    }
    out.data = udp + 8;
    out.size = udpLength - 8;
    ++stats_.datagrams;
    got = true;
}

bool IexCaptureReader::next(Datagram& out) {
    for (;;) {
        bool got = false;
        switch (mode_) {
            case Mode::Start:
                if (!readHeader()) {
                    return false;
                }
                break;
            case Mode::Classic:
                if (!nextClassic(out, got)) {
                    return false;
                }
                break;
            case Mode::Ng:
                if (!nextNg(out, got)) {
                    return false;
                }
                break;
            case Mode::Done:
                return false;
        }
        if (got) {
            return true;
        }
    }
}

}  // namespace quantfinlib
