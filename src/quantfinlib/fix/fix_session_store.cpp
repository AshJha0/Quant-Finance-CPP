#include "quantfinlib/fix/fix_session_store.hpp"

namespace quantfinlib {

std::int64_t InMemorySessionStore::nextOutgoingSeq() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextOutgoing_;
}

std::int64_t InMemorySessionStore::expectedIncomingSeq() {
    std::lock_guard<std::mutex> lock(mutex_);
    return expectedIncoming_;
}

void InMemorySessionStore::saveOutgoingSeq(std::int64_t nextSeq) {
    std::lock_guard<std::mutex> lock(mutex_);
    nextOutgoing_ = nextSeq;
}

void InMemorySessionStore::saveIncomingSeq(std::int64_t expectedSeq) {
    std::lock_guard<std::mutex> lock(mutex_);
    expectedIncoming_ = expectedSeq;
}

void InMemorySessionStore::storeMessage(std::int64_t seq, StoredMessage message) {
    std::lock_guard<std::mutex> lock(mutex_);
    messages_[seq] = std::move(message);
}

std::optional<FixSessionStore::StoredMessage> InMemorySessionStore::retrieve(std::int64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = messages_.find(seq);
    if (it == messages_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::shared_ptr<FixSessionStore> makeInMemorySessionStore() {
    return std::make_shared<InMemorySessionStore>();
}

}  // namespace quantfinlib
