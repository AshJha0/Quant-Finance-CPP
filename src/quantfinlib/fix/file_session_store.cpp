#include "quantfinlib/fix/file_session_store.hpp"

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace quantfinlib {

namespace {

constexpr char MESSAGES_MAGIC[8] = {'Q', 'F', 'S', 'M', '0', '0', '0', '2'};
constexpr std::size_t RECORD_HEADER = 8;  // payload length + payload CRC
constexpr std::size_t SEQ_SLOT = 32;      // outgoing, incoming, generation, CRC, padding
constexpr std::size_t SEQ_SLOT_CRC_INPUT = 24;

std::vector<char> readAll(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    return std::vector<char>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void putString(std::vector<char>& out, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    const char* p = reinterpret_cast<const char*>(&len);
    out.insert(out.end(), p, p + sizeof(len));
    out.insert(out.end(), s.begin(), s.end());
}

bool takeString(const char*& p, const char* end, std::string& out) {
    std::uint32_t len = 0;
    if (static_cast<std::size_t>(end - p) < sizeof(len)) {
        return false;
    }
    std::memcpy(&len, p, sizeof(len));
    p += sizeof(len);
    if (static_cast<std::size_t>(end - p) < len) {
        return false;
    }
    out.assign(p, len);
    p += len;
    return true;
}

}  // namespace

FileSessionStore::FileSessionStore(const std::string& directory, bool syncEveryWrite)
        : syncEveryWrite_(syncEveryWrite) {
    std::filesystem::path dir(directory);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path seqPath = dir / "seqnums.dat";
    std::filesystem::path messagesPath = dir / "messages.dat";

    loadSeqs(seqPath.string());
    loadMessages(messagesPath.string());
    syncParentDirectory(messagesPath.string());
}

void FileSessionStore::loadSeqs(const std::string& path) {
    std::vector<char> data = readAll(path);
    seqFile_.open(path, DurableFile::Mode::Update);
    if (data.empty()) {
        persistSeqs();
        return;
    }
    // Two slots written alternately: a torn write can spoil only the one
    // being written, so the previous state survives in the other.
    bool found = false;
    for (std::size_t slot = 0; slot < 2 && (slot + 1) * SEQ_SLOT <= data.size(); ++slot) {
        const char* p = data.data() + slot * SEQ_SLOT;
        std::uint32_t crc = 0;
        std::memcpy(&crc, p + SEQ_SLOT_CRC_INPUT, sizeof(crc));
        if (crc != crc32(p, SEQ_SLOT_CRC_INPUT)) {
            continue;
        }
        std::uint64_t generation = 0;
        std::memcpy(&generation, p + 16, sizeof(generation));
        if (!found || generation > seqGeneration_) {
            std::memcpy(&nextOutgoing_, p, sizeof(nextOutgoing_));
            std::memcpy(&expectedIncoming_, p + 8, sizeof(expectedIncoming_));
            seqGeneration_ = generation;
            found = true;
        }
    }
    if (!found) {
        // Guessing would reuse sequence numbers on a live session.
        throw std::runtime_error("no valid sequence record in " + path);
    }
}

void FileSessionStore::loadMessages(const std::string& path) {
    std::vector<char> data = readAll(path);
    messageFile_.open(path, DurableFile::Mode::Update);
    if (data.size() < sizeof(MESSAGES_MAGIC)) {
        // New file, or a crash while writing the header.
        messageFile_.truncate(0);
        messageFile_.writeAt(0, MESSAGES_MAGIC, sizeof(MESSAGES_MAGIC));
        messageFile_.sync();
        appendOffset_ = sizeof(MESSAGES_MAGIC);
        return;
    }
    if (std::memcmp(data.data(), MESSAGES_MAGIC, sizeof(MESSAGES_MAGIC)) != 0) {
        throw std::runtime_error(path + " is not a current-format FIX message store");
    }
    std::size_t pos = sizeof(MESSAGES_MAGIC);
    while (data.size() - pos >= RECORD_HEADER) {
        std::uint32_t length = 0;
        std::uint32_t crc = 0;
        std::memcpy(&length, data.data() + pos, sizeof(length));
        std::memcpy(&crc, data.data() + pos + 4, sizeof(crc));
        if (length > data.size() - pos - RECORD_HEADER) {
            break;  // torn tail: the record runs past end of file
        }
        const char* payload = data.data() + pos + RECORD_HEADER;
        std::size_t next = pos + RECORD_HEADER + length;
        if (crc32(payload, length) != crc) {
            if (next == data.size()) {
                break;  // torn tail: last record only partly reached the disk
            }
            // Good data follows, so this is not a crash artefact. Dropping
            // it would silently lose messages that can still be resent.
            throw std::runtime_error(path + ": corrupt record at offset " + std::to_string(pos));
        }
        const char* p = payload;
        const char* end = payload + length;
        std::int64_t seq = 0;
        StoredMessage msg;
        if (length < sizeof(seq)) {
            throw std::runtime_error(path + ": short record at offset " + std::to_string(pos));
        }
        std::memcpy(&seq, p, sizeof(seq));
        p += sizeof(seq);
        if (!takeString(p, end, msg.msgType) || !takeString(p, end, msg.sendingTime)
            || !takeString(p, end, msg.body)) {
            throw std::runtime_error(path + ": bad record at offset " + std::to_string(pos));
        }
        messages_[seq] = std::move(msg);
        pos = next;
    }
    appendOffset_ = pos;
    if (pos != data.size()) {
        // Cut the torn tail now, or later appends would land behind it and
        // be unreachable on the next open.
        messageFile_.truncate(pos);
        messageFile_.sync();
    }
}

FileSessionStore::~FileSessionStore() {
    try {
        close();
    } catch (...) {
        // Destructors must not throw; every record was already synced.
    }
}

std::int64_t FileSessionStore::nextOutgoingSeq() {
    std::lock_guard<std::mutex> lock(mutex_);
    return nextOutgoing_;
}

std::int64_t FileSessionStore::expectedIncomingSeq() {
    std::lock_guard<std::mutex> lock(mutex_);
    return expectedIncoming_;
}

void FileSessionStore::saveOutgoingSeq(std::int64_t nextSeq) {
    std::lock_guard<std::mutex> lock(mutex_);
    nextOutgoing_ = nextSeq;
    persistSeqs();
}

void FileSessionStore::saveIncomingSeq(std::int64_t expectedSeq) {
    std::lock_guard<std::mutex> lock(mutex_);
    expectedIncoming_ = expectedSeq;
    persistSeqs();
}

void FileSessionStore::storeMessage(std::int64_t seq, StoredMessage message) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<char> record(RECORD_HEADER);
    const char* seqBytes = reinterpret_cast<const char*>(&seq);
    record.insert(record.end(), seqBytes, seqBytes + sizeof(seq));
    putString(record, message.msgType);
    putString(record, message.sendingTime);
    putString(record, message.body);
    std::uint32_t length = static_cast<std::uint32_t>(record.size() - RECORD_HEADER);
    std::uint32_t crc = crc32(record.data() + RECORD_HEADER, length);
    std::memcpy(record.data(), &length, sizeof(length));
    std::memcpy(record.data() + 4, &crc, sizeof(crc));
    try {
        messageFile_.writeAt(appendOffset_, record.data(), record.size());
        if (syncEveryWrite_) {
            messageFile_.sync();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("failed to persist FIX message " + std::to_string(seq) + ": "
                                 + e.what());
    }
    appendOffset_ += record.size();
    messages_[seq] = std::move(message);
}

std::optional<FixSessionStore::StoredMessage> FileSessionStore::retrieve(std::int64_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = messages_.find(seq);
    if (it == messages_.end()) {
        return std::nullopt;
    }
    return it->second;
}

void FileSessionStore::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    // Counters first: a crash between the two steps then leaves old messages
    // under new numbers only until the next reset, never new numbers lost.
    nextOutgoing_ = 1;
    expectedIncoming_ = 1;
    persistSeqs();
    messageFile_.truncate(sizeof(MESSAGES_MAGIC));
    messageFile_.sync();
    appendOffset_ = sizeof(MESSAGES_MAGIC);
    messages_.clear();
}

void FileSessionStore::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    messageFile_.close();
    seqFile_.close();
}

void FileSessionStore::persistSeqs() {
    ++seqGeneration_;
    std::array<char, SEQ_SLOT> slot{};
    std::memcpy(slot.data(), &nextOutgoing_, sizeof(nextOutgoing_));
    std::memcpy(slot.data() + 8, &expectedIncoming_, sizeof(expectedIncoming_));
    std::memcpy(slot.data() + 16, &seqGeneration_, sizeof(seqGeneration_));
    std::uint32_t crc = crc32(slot.data(), SEQ_SLOT_CRC_INPUT);
    std::memcpy(slot.data() + SEQ_SLOT_CRC_INPUT, &crc, sizeof(crc));
    try {
        seqFile_.writeAt((seqGeneration_ % 2) * SEQ_SLOT, slot.data(), slot.size());
        if (syncEveryWrite_) {
            seqFile_.sync();
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("failed to persist FIX sequence numbers: ") + e.what());
    }
}

}  // namespace quantfinlib
