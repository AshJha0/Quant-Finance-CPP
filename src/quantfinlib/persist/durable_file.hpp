// Small durable-write helpers shared by Checkpoint and FileSessionStore.
// std::fstream can flush to the OS but cannot force bytes to the disk, so
// anything that must survive a power cut goes through DurableFile: a raw
// file descriptor with checked writes, fsync (_commit on Windows) and
// truncate. Every failure throws std::runtime_error.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace quantfinlib {

/** CRC-32 (IEEE 802.3, the zlib polynomial). Pass the previous value to chain. */
std::uint32_t crc32(const void* data, std::size_t length, std::uint32_t seed = 0);

class DurableFile {
public:
    enum class Mode {
        Truncate,  // create or empty
        Update     // create if missing, keep the content
    };

    DurableFile() = default;
    DurableFile(const std::string& path, Mode mode);
    ~DurableFile();

    DurableFile(const DurableFile&) = delete;
    DurableFile& operator=(const DurableFile&) = delete;

    bool isOpen() const { return fd_ >= 0; }

    void open(const std::string& path, Mode mode);

    /** Writes every byte at `offset`; a short or failed write throws. */
    void writeAt(std::uint64_t offset, const void* data, std::size_t length);

    /** Forces written data to stable storage. */
    void sync();

    void truncate(std::uint64_t size);

    /** Closes the descriptor, reporting a failed close. */
    void close();

private:
    int fd_ = -1;
    std::string path_;
};

/** fsyncs the directory holding `path`, making a create or rename durable. No-op on Windows. */
void syncParentDirectory(const std::string& path);

}  // namespace quantfinlib
