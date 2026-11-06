// Port of com.quantfinlib.fix.FixSessionStore (Java): persistence seam for a
// FixSession -- sequence numbers and the outbound application-message store.
// With a durable implementation (FileSessionStore) a session reconnects with
// sequence-number continuation -- the peer's ResendRequest is then serviced
// from messages sent before the restart. Methods are invoked from the
// session's writer path (outgoing) and reader thread (incoming);
// implementations must be thread-safe.
#pragma once

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace quantfinlib {

class FixSessionStore {
public:
    /** One stored outbound application message, replayable on ResendRequest. */
    struct StoredMessage {
        std::string msgType;
        std::string body;
        std::string sendingTime;
    };

    virtual ~FixSessionStore() = default;

    /** The next outgoing MsgSeqNum to use (1 for a fresh session). */
    virtual std::int64_t nextOutgoingSeq() = 0;

    /** The next incoming MsgSeqNum expected (1 for a fresh session). */
    virtual std::int64_t expectedIncomingSeq() = 0;

    virtual void saveOutgoingSeq(std::int64_t nextSeq) = 0;

    virtual void saveIncomingSeq(std::int64_t expectedSeq) = 0;

    virtual void storeMessage(std::int64_t seq, StoredMessage message) = 0;

    /** The stored message at seq, or std::nullopt (admin / never sent). */
    virtual std::optional<StoredMessage> retrieve(std::int64_t seq) = 0;

    /**
     * Clean slate for ResetSeqNumFlag(141)=Y: both counters back to 1 and
     * every stored message dropped, so a later ResendRequest can never
     * replay an order from the abandoned sequence.
     */
    virtual void reset() = 0;
};

/** Fresh in-memory store: per-connection sequence numbers (the default). */
class InMemorySessionStore : public FixSessionStore {
public:
    std::int64_t nextOutgoingSeq() override;
    std::int64_t expectedIncomingSeq() override;
    void saveOutgoingSeq(std::int64_t nextSeq) override;
    void saveIncomingSeq(std::int64_t expectedSeq) override;
    void storeMessage(std::int64_t seq, StoredMessage message) override;
    std::optional<StoredMessage> retrieve(std::int64_t seq) override;
    void reset() override;

private:
    std::mutex mutex_;
    std::unordered_map<std::int64_t, StoredMessage> messages_;
    std::int64_t nextOutgoing_ = 1;
    std::int64_t expectedIncoming_ = 1;
};

/** Fresh in-memory store, shared-ownership factory mirroring FixSessionStore.inMemory(). */
std::shared_ptr<FixSessionStore> makeInMemorySessionStore();

}  // namespace quantfinlib
