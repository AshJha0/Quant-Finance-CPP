#include "quantfinlib/fix/file_session_store.hpp"

#include <filesystem>
#include <stdexcept>

namespace quantfinlib {

namespace {

void writeString(std::ostream& out, const std::string& s) {
    std::uint32_t len = static_cast<std::uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    out.write(s.data(), static_cast<std::streamsize>(s.size()));
}

bool readString(std::istream& in, std::string& out) {
    std::uint32_t len = 0;
    if (!in.read(reinterpret_cast<char*>(&len), sizeof(len))) {
        return false;
    }
    out.resize(len);
    if (len > 0 && !in.read(out.data(), len)) {
        return false;
    }
    return true;
}

}  // namespace

FileSessionStore::FileSessionStore(const std::string& directory) {
    std::filesystem::path dir(directory);
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    std::filesystem::path seqPath = dir / "seqnums.dat";
    std::filesystem::path messagesPath = dir / "messages.dat";

    if (!std::filesystem::exists(seqPath)) {
        // Create the file first: fstream cannot create a missing file when
        // opened with in|out.
        std::ofstream create(seqPath, std::ios::binary);
        create.close();
    }
    seqFile_.open(seqPath, std::ios::binary | std::ios::in | std::ios::out);
    if (!seqFile_.is_open()) {
        throw std::runtime_error("failed to open " + seqPath.string());
    }
    std::error_code sizeEc;
    auto size = std::filesystem::file_size(seqPath, sizeEc);
    if (!sizeEc && size >= 16) {
        seqFile_.seekg(0);
        seqFile_.read(reinterpret_cast<char*>(&nextOutgoing_), sizeof(nextOutgoing_));
        seqFile_.read(reinterpret_cast<char*>(&expectedIncoming_), sizeof(expectedIncoming_));
    } else {
        persistSeqs();
    }

    if (std::filesystem::exists(messagesPath)) {
        std::ifstream in(messagesPath, std::ios::binary);
        while (in.good()) {
            std::int64_t seq = 0;
            if (!in.read(reinterpret_cast<char*>(&seq), sizeof(seq))) {
                break;
            }
            StoredMessage msg;
            if (!readString(in, msg.msgType) || !readString(in, msg.sendingTime)
                || !readString(in, msg.body)) {
                break;
            }
            messages_[seq] = std::move(msg);
        }
    }
    messageAppender_.open(messagesPath, std::ios::binary | std::ios::app);
    if (!messageAppender_.is_open()) {
        throw std::runtime_error("failed to open " + messagesPath.string());
    }
}

FileSessionStore::~FileSessionStore() {
    close();
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
    messageAppender_.write(reinterpret_cast<const char*>(&seq), sizeof(seq));
    writeString(messageAppender_, message.msgType);
    writeString(messageAppender_, message.sendingTime);
    writeString(messageAppender_, message.body);
    messageAppender_.flush();
    if (!messageAppender_.good()) {
        throw std::runtime_error("failed to persist FIX message " + std::to_string(seq));
    }
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

void FileSessionStore::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (closed_) {
        return;
    }
    closed_ = true;
    if (messageAppender_.is_open()) {
        messageAppender_.close();
    }
    if (seqFile_.is_open()) {
        seqFile_.close();
    }
}

void FileSessionStore::persistSeqs() {
    seqFile_.seekp(0);
    seqFile_.write(reinterpret_cast<const char*>(&nextOutgoing_), sizeof(nextOutgoing_));
    seqFile_.write(reinterpret_cast<const char*>(&expectedIncoming_), sizeof(expectedIncoming_));
    seqFile_.flush();
    if (!seqFile_.good()) {
        throw std::runtime_error("failed to persist FIX sequence numbers");
    }
}

}  // namespace quantfinlib
