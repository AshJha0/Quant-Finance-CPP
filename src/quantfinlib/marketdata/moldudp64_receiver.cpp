#include "quantfinlib/marketdata/moldudp64_receiver.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {

int u16(const unsigned char* b, int i) {
    return (b[i] << 8) | b[i + 1];
}

std::uint64_t u64(const unsigned char* b, int i) {
    std::uint64_t v = 0;
    for (int k = 0; k < 8; ++k) {
        v = (v << 8) | b[i + k];
    }
    return v;
}

}  // namespace

MoldUdp64Receiver::MoldUdp64Receiver(MessageHandler& handler, Recovery& recovery,
                                     int maxRequestCount, std::int64_t retransmitTimeoutNanos,
                                     int maxRetransmitAttempts)
        : handler_(handler),
          recovery_(recovery),
          maxRequestCount_(maxRequestCount),
          retransmitTimeoutNanos_(retransmitTimeoutNanos),
          maxRetransmitAttempts_(maxRetransmitAttempts) {
    if (maxRequestCount < 1 || maxRequestCount > 0xFFFE || maxRetransmitAttempts < 1) {
        throw std::invalid_argument("need 1 <= maxRequestCount <= 65534, attempts >= 1");
    }
}

void MoldUdp64Receiver::onPacket(const unsigned char* buf, int offset, int length,
                                 std::int64_t nowNanos) {
    if (buf == nullptr || length < HEADER_LENGTH || offset < 0) {
        malformedPackets_++;
        return;
    }
    if (!sessionKnown_) {
        std::memcpy(session_, buf + offset, sizeof(session_));
        sessionName_.assign(reinterpret_cast<const char*>(session_), sizeof(session_));
        std::size_t last = sessionName_.find_last_not_of(' ');
        sessionName_.erase(last == std::string::npos ? 0 : last + 1);
        sessionKnown_ = true;
    } else if (std::memcmp(buf + offset, session_, sizeof(session_)) != 0) {
        foreignSessionPackets_++;
        return;
    }
    std::uint64_t wireFirst = u64(buf, offset + 10);
    int count = u16(buf, offset + 18);
    if (count == END_OF_SESSION) {
        endOfSession_ = true;
        count = 0;
    }
    // The upper bound keeps first + count from overflowing below.
    constexpr std::uint64_t maxFirst =
            static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) - 0xFFFF;
    if (wireFirst < 1 || wireFirst > maxFirst) {
        malformedPackets_++;
        return;
    }
    if (awaitingSnapshot_) {
        return;  // nothing applies until the snapshot is in
    }
    std::int64_t first = static_cast<std::int64_t>(wireFirst);
    std::int64_t next = first + count;  // first sequence after this packet
    if (first > expected_) {
        gapsDetected_ += recovering_ ? 0 : 1;
        gapEnd_ = std::max(gapEnd_, recovering_ ? next : std::max(first, next));
        if (!recovering_) {
            recovering_ = true;
            attempts_ = 0;
            request(nowNanos);
        }
        return;
    }
    if (count == 0) {
        return;  // heartbeat or end of session at or below expected: nothing missed
    }
    if (next <= expected_) {
        duplicatePackets_++;
        return;
    }
    // Walk the blocks; deliver those at or above expected.
    int remaining = length - HEADER_LENGTH;
    int p = offset + HEADER_LENGTH;
    std::int64_t seq = first;
    for (int i = 0; i < count; ++i) {
        if (remaining < 2) {
            malformedPackets_++;
            return;
        }
        int len = u16(buf, p);
        p += 2;
        remaining -= 2;
        if (len > remaining) {
            malformedPackets_++;
            return;
        }
        if (seq == expected_) {
            handler_.onMessage(buf, p, len, seq);
            messagesDelivered_++;
            expected_ = seq + 1;
        }
        p += len;
        remaining -= len;
        seq++;
    }
    if (recovering_ && expected_ >= gapEnd_) {
        recovering_ = false;  // gap closed
    } else if (recovering_) {
        // Partly closed: the next request starts from the new expected.
        attempts_ = 0;
        requestedAtNanos_ = nowNanos;
    }
}

void MoldUdp64Receiver::onTimer(std::int64_t nowNanos) {
    if (!recovering_ || awaitingSnapshot_) {
        return;
    }
    if (nowNanos - requestedAtNanos_ < retransmitTimeoutNanos_) {
        return;
    }
    if (attempts_ >= maxRetransmitAttempts_) {
        awaitingSnapshot_ = true;
        recovery_.rebuildFromSnapshot(sessionName_, expected_);
        return;
    }
    request(nowNanos);
}

void MoldUdp64Receiver::resumeAfterSnapshot(std::int64_t nextSequence) {
    expected_ = nextSequence;
    recovering_ = false;
    awaitingSnapshot_ = false;
    gapEnd_ = 0;
    attempts_ = 0;
}

void MoldUdp64Receiver::request(std::int64_t nowNanos) {
    attempts_++;
    requestedAtNanos_ = nowNanos;
    std::int64_t missing = std::max<std::int64_t>(1, gapEnd_ - expected_);
    recovery_.requestRetransmit(
            sessionName_, expected_,
            static_cast<int>(std::min<std::int64_t>(missing, maxRequestCount_)));
}

int MoldUdp64Receiver::encodeRequest(unsigned char* buf, int offset, std::string_view session,
                                     std::int64_t fromSequence, int count) {
    if (session.size() > 10) {
        throw std::invalid_argument("session longer than 10 characters");
    }
    for (std::size_t i = 0; i < 10; ++i) {
        buf[offset + static_cast<int>(i)] =
                i < session.size() ? static_cast<unsigned char>(session[i]) : ' ';
    }
    std::uint64_t v = static_cast<std::uint64_t>(fromSequence);
    for (int k = 7; k >= 0; --k) {
        buf[offset + 10 + k] = static_cast<unsigned char>(v);
        v >>= 8;
    }
    buf[offset + 18] = static_cast<unsigned char>(count >> 8);
    buf[offset + 19] = static_cast<unsigned char>(count);
    return HEADER_LENGTH;
}

int MoldUdp64Receiver::encodeBlock(unsigned char* buf, int offset, const unsigned char* payload,
                                   int payloadOffset, int payloadLength) {
    buf[offset] = static_cast<unsigned char>(payloadLength >> 8);
    buf[offset + 1] = static_cast<unsigned char>(payloadLength);
    std::memcpy(buf + offset + 2, payload + payloadOffset, static_cast<std::size_t>(payloadLength));
    return payloadLength + 2;
}

}  // namespace quantfinlib
