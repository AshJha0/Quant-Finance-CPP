// Port of com.quantfinlib.feed.MoldUdp64Receiver (Java): unpacks MoldUDP64
// downstream packets into sequenced messages, detects gaps and drives
// recovery (Nasdaq "MoldUDP64 Protocol Specification" v1.00, 2009).
//
//   packet  = session (10 bytes ASCII) | sequence (uint64 BE, of the first
//             message) | count (uint16 BE) | count x message block
//   message = length (uint16 BE) | payload (e.g. one ITCH message)
//   count 0 = heartbeat; count 0xFFFF = end of session
//
// Messages reach the handler strictly in sequence. A packet above the next
// expected number opens a gap: its messages are dropped, requestRetransmit is
// called for the missing range (at most maxRequestCount messages) and
// delivery resumes when the retransmission arrives. Packets wholly below the
// expected number are duplicates (the A/B arbitration case); a packet
// straddling it delivers only its new messages.
//
// If the gap stays open for retransmitTimeoutNanos, onTimer re-requests, and
// after maxRetransmitAttempts requests calls rebuildFromSnapshot: the owner
// clears its book, replays a snapshot and calls resumeAfterSnapshot.
//
// No allocation per packet. Single thread (the feed handler).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace quantfinlib {

class MoldUdp64Receiver {
public:
    static constexpr int HEADER_LENGTH = 20;
    static constexpr int END_OF_SESSION = 0xFFFF;

    class MessageHandler {
    public:
        virtual ~MessageHandler() = default;
        virtual void onMessage(const unsigned char* buf, int offset, int length,
                               std::int64_t sequence) = 0;
    };

    class Recovery {
    public:
        virtual ~Recovery() = default;
        /** Ask the re-request server for `count` messages from `fromSequence`. */
        virtual void requestRetransmit(const std::string& session, std::int64_t fromSequence,
                                       int count) = 0;
        /** Retransmission failed: rebuild from a snapshot, then call resumeAfterSnapshot. */
        virtual void rebuildFromSnapshot(const std::string& session,
                                         std::int64_t missingFromSequence) = 0;
    };

    /**
     * Handler and recovery must outlive the receiver. Throws
     * std::invalid_argument unless 1 <= maxRequestCount <= 65534 and
     * maxRetransmitAttempts >= 1.
     */
    MoldUdp64Receiver(MessageHandler& handler, Recovery& recovery, int maxRequestCount,
                      std::int64_t retransmitTimeoutNanos, int maxRetransmitAttempts);

    /** Processes one datagram of `length` bytes. nowNanos is a monotonic clock. */
    void onPacket(const unsigned char* buf, int offset, int length, std::int64_t nowNanos);

    /** Re-requests or falls back to a snapshot when recovery has timed out. */
    void onTimer(std::int64_t nowNanos);

    /** Resumes delivery after a snapshot current up to (excluding) nextSequence. */
    void resumeAfterSnapshot(std::int64_t nextSequence);

    /** Encodes a request packet (session, sequence, count); returns 20. Throws past 10 chars. */
    static int encodeRequest(unsigned char* buf, int offset, std::string_view session,
                             std::int64_t fromSequence, int count);

    /** Encodes a downstream header; message blocks follow at offset + 20. */
    static int encodeHeader(unsigned char* buf, int offset, std::string_view session,
                            std::int64_t firstSequence, int count) {
        return encodeRequest(buf, offset, session, firstSequence, count);
    }

    /** Appends one message block at `offset`; returns bytes written. */
    static int encodeBlock(unsigned char* buf, int offset, const unsigned char* payload,
                           int payloadOffset, int payloadLength);

    std::int64_t expectedSequence() const { return expected_; }
    bool isRecovering() const { return recovering_; }
    bool isAwaitingSnapshot() const { return awaitingSnapshot_; }
    bool isEndOfSession() const { return endOfSession_; }
    std::int64_t gapsDetected() const { return gapsDetected_; }
    std::int64_t duplicatePackets() const { return duplicatePackets_; }
    std::int64_t malformedPackets() const { return malformedPackets_; }
    std::int64_t foreignSessionPackets() const { return foreignSessionPackets_; }
    std::int64_t messagesDelivered() const { return messagesDelivered_; }
    const std::string& session() const { return sessionName_; }

private:
    void request(std::int64_t nowNanos);

    MessageHandler& handler_;
    Recovery& recovery_;
    int maxRequestCount_;
    std::int64_t retransmitTimeoutNanos_;
    int maxRetransmitAttempts_;
    unsigned char session_[10] = {};
    bool sessionKnown_ = false;
    std::string sessionName_;
    std::int64_t expected_ = 1;
    bool recovering_ = false;
    bool awaitingSnapshot_ = false;
    std::int64_t gapEnd_ = 0;  // exclusive end of the missing range
    std::int64_t requestedAtNanos_ = 0;
    int attempts_ = 0;
    bool endOfSession_ = false;
    std::int64_t gapsDetected_ = 0;
    std::int64_t duplicatePackets_ = 0;
    std::int64_t malformedPackets_ = 0;
    std::int64_t foreignSessionPackets_ = 0;
    std::int64_t messagesDelivered_ = 0;
};

}  // namespace quantfinlib
