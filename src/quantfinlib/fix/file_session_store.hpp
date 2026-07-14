// Port of com.quantfinlib.fix.FileSessionStore (Java): durable
// FixSessionStore backed by two files in a directory: seqnums.dat (16
// bytes, flushed synchronously on every update -- the "rwd" mode of the
// Java RandomAccessFile) and an append-only messages.dat replayed into
// memory on open. Reopening the store after a process restart resumes the
// FIX session with sequence-number continuation and a fully serviceable
// resend history. The on-disk record format is private to this port (not
// byte-compatible with the Java store -- there is no cross-runtime file
// interop requirement).
#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

#include "quantfinlib/fix/fix_session_store.hpp"

namespace quantfinlib {

class FileSessionStore : public FixSessionStore {
public:
    /** Throws std::runtime_error if the directory or files cannot be opened. */
    explicit FileSessionStore(const std::string& directory);
    ~FileSessionStore() override;

    FileSessionStore(const FileSessionStore&) = delete;
    FileSessionStore& operator=(const FileSessionStore&) = delete;

    std::int64_t nextOutgoingSeq() override;
    std::int64_t expectedIncomingSeq() override;
    void saveOutgoingSeq(std::int64_t nextSeq) override;
    void saveIncomingSeq(std::int64_t expectedSeq) override;
    void storeMessage(std::int64_t seq, StoredMessage message) override;
    std::optional<StoredMessage> retrieve(std::int64_t seq) override;

    void close();

private:
    void persistSeqs();

    std::mutex mutex_;
    std::fstream seqFile_;
    std::ofstream messageAppender_;
    std::unordered_map<std::int64_t, StoredMessage> messages_;
    std::int64_t nextOutgoing_ = 1;
    std::int64_t expectedIncoming_ = 1;
    bool closed_ = false;
};

}  // namespace quantfinlib
