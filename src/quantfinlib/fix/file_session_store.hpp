// Port of com.quantfinlib.fix.FileSessionStore (Java): durable
// FixSessionStore backed by two files in a directory.
//
// seqnums.dat holds two CRC-protected slots written alternately, so a torn
// write always leaves the previous counters readable. messages.dat is a
// magic header then an append-only run of length + CRC records, replayed
// into memory on open. A torn tail (crash mid-append) is truncated away; a
// bad record with good data after it is real corruption and fails the open.
// Every write is fsynced unless the constructor is told otherwise.
//
// The on-disk format is private to this port (not byte-compatible with the
// Java store) and has no reader for the earlier unchecked C++ layout: such
// a directory fails to open rather than being misread.
#pragma once

#include <mutex>
#include <string>
#include <unordered_map>

#include "quantfinlib/fix/fix_session_store.hpp"
#include "quantfinlib/persist/durable_file.hpp"

namespace quantfinlib {

class FileSessionStore : public FixSessionStore {
public:
    /**
     * Throws std::runtime_error if the directory or files cannot be opened
     * or fail their integrity checks. syncEveryWrite = false leaves durability
     * to the OS, for tests and replay tools only.
     */
    explicit FileSessionStore(const std::string& directory, bool syncEveryWrite = true);
    ~FileSessionStore() override;

    FileSessionStore(const FileSessionStore&) = delete;
    FileSessionStore& operator=(const FileSessionStore&) = delete;

    std::int64_t nextOutgoingSeq() override;
    std::int64_t expectedIncomingSeq() override;
    void saveOutgoingSeq(std::int64_t nextSeq) override;
    void saveIncomingSeq(std::int64_t expectedSeq) override;
    void storeMessage(std::int64_t seq, StoredMessage message) override;
    std::optional<StoredMessage> retrieve(std::int64_t seq) override;
    void reset() override;

    /** Later writes throw. Safe to call twice. */
    void close();

private:
    void loadSeqs(const std::string& path);
    void loadMessages(const std::string& path);
    void persistSeqs();

    std::mutex mutex_;
    DurableFile seqFile_;
    DurableFile messageFile_;
    std::unordered_map<std::int64_t, StoredMessage> messages_;
    std::int64_t nextOutgoing_ = 1;
    std::int64_t expectedIncoming_ = 1;
    std::uint64_t seqGeneration_ = 0;
    std::uint64_t appendOffset_ = 0;
    bool syncEveryWrite_;
};

}  // namespace quantfinlib
