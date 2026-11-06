#include "quantfinlib/persist/durable_file.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <stdexcept>

#include <fcntl.h>
#ifdef _WIN32
#include <io.h>
#include <share.h>
#include <sys/stat.h>
#else
#include <unistd.h>
#endif

namespace quantfinlib {

namespace {

std::array<std::uint32_t, 256> makeCrcTable() {
    std::array<std::uint32_t, 256> table{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) {
            c = (c & 1U) != 0 ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}

[[noreturn]] void fail(const std::string& what, const std::string& path) {
    throw std::runtime_error(what + " " + path + ": " + std::strerror(errno));
}

}  // namespace

std::uint32_t crc32(const void* data, std::size_t length, std::uint32_t seed) {
    static const std::array<std::uint32_t, 256> table = makeCrcTable();
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::uint32_t c = ~seed;
    for (std::size_t i = 0; i < length; ++i) {
        c = table[(c ^ p[i]) & 0xFFU] ^ (c >> 8);
    }
    return ~c;
}

DurableFile::DurableFile(const std::string& path, Mode mode) {
    open(path, mode);
}

DurableFile::~DurableFile() {
    try {
        close();
    } catch (...) {
        // Destructors must not throw; call close() to observe the failure.
    }
}

void DurableFile::open(const std::string& path, Mode mode) {
    close();
    path_ = path;
#ifdef _WIN32
    int flags = _O_RDWR | _O_CREAT | _O_BINARY | (mode == Mode::Truncate ? _O_TRUNC : 0);
    if (_sopen_s(&fd_, path.c_str(), flags, _SH_DENYNO, _S_IREAD | _S_IWRITE) != 0) {
        fd_ = -1;
    }
#else
    int flags = O_RDWR | O_CREAT | O_CLOEXEC | (mode == Mode::Truncate ? O_TRUNC : 0);
    fd_ = ::open(path.c_str(), flags, 0644);
#endif
    if (fd_ < 0) {
        fail("cannot open", path);
    }
}

void DurableFile::writeAt(std::uint64_t offset, const void* data, std::size_t length) {
    if (fd_ < 0) {
        throw std::runtime_error("write to closed file " + path_);
    }
    const char* p = static_cast<const char*>(data);
#ifdef _WIN32
    if (_lseeki64(fd_, static_cast<__int64>(offset), SEEK_SET) < 0) {
        fail("cannot seek", path_);
    }
#endif
    while (length > 0) {
#ifdef _WIN32
        int n = _write(fd_, p, static_cast<unsigned>(length > (1U << 30) ? (1U << 30) : length));
#else
        ssize_t n = ::pwrite(fd_, p, length, static_cast<off_t>(offset));
#endif
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            fail("cannot write", path_);
        }
        p += n;
        offset += static_cast<std::uint64_t>(n);
        length -= static_cast<std::size_t>(n);
    }
}

void DurableFile::sync() {
    if (fd_ < 0) {
        throw std::runtime_error("sync of closed file " + path_);
    }
#ifdef _WIN32
    int rc = _commit(fd_);
#else
    int rc = ::fsync(fd_);
#endif
    if (rc != 0) {
        fail("cannot sync", path_);
    }
}

void DurableFile::truncate(std::uint64_t size) {
    if (fd_ < 0) {
        throw std::runtime_error("truncate of closed file " + path_);
    }
#ifdef _WIN32
    int rc = _chsize_s(fd_, static_cast<__int64>(size));
#else
    int rc = ::ftruncate(fd_, static_cast<off_t>(size));
#endif
    if (rc != 0) {
        fail("cannot truncate", path_);
    }
}

void DurableFile::close() {
    if (fd_ < 0) {
        return;
    }
    int fd = fd_;
    fd_ = -1;
#ifdef _WIN32
    int rc = _close(fd);
#else
    int rc = ::close(fd);
#endif
    if (rc != 0) {
        fail("cannot close", path_);
    }
}

void syncParentDirectory(const std::string& path) {
#ifndef _WIN32
    std::filesystem::path parent = std::filesystem::path(path).parent_path();
    std::string dir = parent.empty() ? "." : parent.string();
    int fd = ::open(dir.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (fd < 0) {
        fail("cannot open directory", dir);
    }
    int rc = ::fsync(fd);
    int saved = errno;
    ::close(fd);
    // Some filesystems refuse fsync on a directory; that is not a data error.
    if (rc != 0 && saved != EINVAL && saved != ENOTSUP) {
        errno = saved;
        fail("cannot sync directory", dir);
    }
#else
    (void)path;
#endif
}

}  // namespace quantfinlib
