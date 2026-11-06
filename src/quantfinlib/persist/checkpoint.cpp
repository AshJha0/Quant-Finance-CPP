#include "quantfinlib/persist/checkpoint.hpp"

#include <cstring>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <stdexcept>

#include "quantfinlib/persist/durable_file.hpp"

namespace quantfinlib {

// ------------------------------------------------------------- BinWriter

void BinWriter::writeByte(std::int8_t v) {
    buf_.push_back(static_cast<std::uint8_t>(v));
}

void BinWriter::writeInt(std::int32_t v) {
    std::uint32_t u = static_cast<std::uint32_t>(v);
    buf_.push_back(static_cast<std::uint8_t>(u >> 24));
    buf_.push_back(static_cast<std::uint8_t>(u >> 16));
    buf_.push_back(static_cast<std::uint8_t>(u >> 8));
    buf_.push_back(static_cast<std::uint8_t>(u));
}

void BinWriter::writeLong(std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 7; i >= 0; i--) {
        buf_.push_back(static_cast<std::uint8_t>(u >> (8 * i)));
    }
}

void BinWriter::writeDouble(double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    writeLong(static_cast<std::int64_t>(bits));
}

// ------------------------------------------------------------- BinReader

namespace {

void requireBytes(std::size_t remaining, std::size_t need) {
    if (remaining < need) {
        throw std::runtime_error("checkpoint payload truncated");
    }
}

}  // namespace

std::int8_t BinReader::readByte() {
    requireBytes(remaining(), 1);
    return static_cast<std::int8_t>(buf_[pos_++]);
}

std::int32_t BinReader::readInt() {
    requireBytes(remaining(), 4);
    std::uint32_t u = 0;
    for (int i = 0; i < 4; i++) {
        u = (u << 8) | buf_[pos_++];
    }
    return static_cast<std::int32_t>(u);
}

std::int64_t BinReader::readLong() {
    requireBytes(remaining(), 8);
    std::uint64_t u = 0;
    for (int i = 0; i < 8; i++) {
        u = (u << 8) | buf_[pos_++];
    }
    return static_cast<std::int64_t>(u);
}

double BinReader::readDouble() {
    std::int64_t bits = readLong();
    std::uint64_t u = static_cast<std::uint64_t>(bits);
    double v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

// ------------------------------------------------------------- Writer

Checkpoint::Writer::Writer(std::string target)
        : target_(std::move(target)), uncaughtAtStart_(std::uncaught_exceptions()) {}

Checkpoint::Writer::~Writer() {
    // Unwinding means the caller never finished the snapshot; committing it
    // would replace a good checkpoint with a partial one.
    if (std::uncaught_exceptions() > uncaughtAtStart_) {
        return;
    }
    try {
        close();
    } catch (...) {
        // Destructor must not throw; call close() explicitly to observe
        // commit failures (mirrors this port's TickFileWriter convention).
    }
}

Checkpoint::Writer& Checkpoint::Writer::section(const std::string& name, const StateWriter& body) {
    if (name == CRC_SECTION || name.size() > 0xFFFF) {
        failed_ = true;
        throw std::invalid_argument("invalid checkpoint section name: " + name);
    }
    for (const auto& kv : sections_) {
        if (kv.first == name) {
            failed_ = true;
            throw std::invalid_argument("duplicate checkpoint section: " + name);
        }
    }
    BinWriter w;
    try {
        body(w);
    } catch (...) {
        failed_ = true;
        throw;
    }
    if (w.bytes().size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) {
        failed_ = true;
        throw std::invalid_argument("checkpoint section too large: " + name);
    }
    sections_.emplace_back(name, w.bytes());
    return *this;
}

namespace {

void putU16BE(std::vector<std::uint8_t>& out, std::uint16_t v) {
    out.push_back(static_cast<std::uint8_t>(v >> 8));
    out.push_back(static_cast<std::uint8_t>(v));
}

void putI32BE(std::vector<std::uint8_t>& out, std::int32_t v) {
    std::uint32_t u = static_cast<std::uint32_t>(v);
    for (int shift = 24; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(u >> shift));
    }
}

void putSection(std::vector<std::uint8_t>& out, const std::string& name,
                const std::vector<std::uint8_t>& body) {
    putU16BE(out, static_cast<std::uint16_t>(name.size()));
    out.insert(out.end(), name.begin(), name.end());
    putI32BE(out, static_cast<std::int32_t>(body.size()));
    out.insert(out.end(), body.begin(), body.end());
}

}  // namespace

void Checkpoint::Writer::close() {
    if (closed_) {
        return;
    }
    closed_ = true;
    if (failed_) {
        return;
    }
    std::vector<std::uint8_t> image;
    putI32BE(image, MAGIC);
    putI32BE(image, FORMAT_VERSION);
    for (const auto& kv : sections_) {
        putSection(image, kv.first, kv.second);
    }
    // The trailer is an ordinary section, so format-1 readers in other
    // languages skip it as a section they never ask for. Its CRC covers
    // every byte before the trailer's own 4-byte payload.
    std::string crcName(CRC_SECTION);
    putU16BE(image, static_cast<std::uint16_t>(crcName.size()));
    image.insert(image.end(), crcName.begin(), crcName.end());
    putI32BE(image, 4);
    putI32BE(image, static_cast<std::int32_t>(crc32(image.data(), image.size())));

    std::string temp = target_ + ".tmp";
    std::filesystem::path tempPath(temp);
    if (tempPath.has_parent_path()) {
        std::filesystem::create_directories(tempPath.parent_path());
    }
    try {
        DurableFile out(temp, DurableFile::Mode::Truncate);
        out.writeAt(0, image.data(), image.size());
        out.sync();
        out.close();
    } catch (const std::exception& e) {
        std::error_code rmEc;
        std::filesystem::remove(temp, rmEc);
        throw std::runtime_error("failed writing checkpoint temp file: " + std::string(e.what()));
    }
    // rename replaces the target atomically. There is deliberately no
    // remove-then-rename fallback: it would open a window with no checkpoint
    // at all. On failure the previous checkpoint stays in place.
    std::error_code ec;
    std::filesystem::rename(temp, target_, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(temp, rmEc);
        throw std::runtime_error("failed to commit checkpoint: " + target_ + " (" + ec.message() + ")");
    }
    syncParentDirectory(target_);
}

// ------------------------------------------------------------- Reader

Checkpoint::Reader::Reader(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(path + " is not a quantfinlib checkpoint");
    }
    std::vector<std::uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (data.size() < 8) {
        throw std::runtime_error(path + " is not a quantfinlib checkpoint");
    }
    auto readI32 = [&](std::size_t pos) -> std::int32_t {
        std::uint32_t u = (static_cast<std::uint32_t>(data[pos]) << 24)
                | (static_cast<std::uint32_t>(data[pos + 1]) << 16)
                | (static_cast<std::uint32_t>(data[pos + 2]) << 8)
                | static_cast<std::uint32_t>(data[pos + 3]);
        return static_cast<std::int32_t>(u);
    };
    std::int32_t magic = readI32(0);
    if (magic != MAGIC) {
        throw std::runtime_error(path + " is not a quantfinlib checkpoint");
    }
    std::int32_t version = readI32(4);
    if (version < 1 || version > FORMAT_VERSION) {
        throw std::runtime_error(path + " uses checkpoint format " + std::to_string(version)
                                  + "; this build reads up to " + std::to_string(FORMAT_VERSION));
    }
    std::size_t pos = 8;
    std::size_t n = data.size();
    while (pos < n) {
        if (pos + 2 > n) {
            throw std::runtime_error(path + ": truncated section header");
        }
        std::uint16_t nameLen = static_cast<std::uint16_t>((static_cast<unsigned>(data[pos]) << 8)
                                                            | static_cast<unsigned>(data[pos + 1]));
        pos += 2;
        if (pos + nameLen > n) {
            throw std::runtime_error(path + ": truncated section name");
        }
        std::string name(reinterpret_cast<const char*>(&data[pos]), nameLen);
        pos += nameLen;
        if (pos + 4 > n) {
            throw std::runtime_error(path + ": truncated section length");
        }
        std::int32_t length = readI32(pos);
        pos += 4;
        if (length < 0 || pos + static_cast<std::size_t>(length) > n) {
            throw std::runtime_error(path + ": section '" + name + "' declares " + std::to_string(length)
                                      + " bytes, " + std::to_string(n - pos) + " remain");
        }
        if (name == CRC_SECTION) {
            // pos is at the payload; the CRC covers everything before it.
            if (length != 4 || pos + 4 != n) {
                throw std::runtime_error(path + ": misplaced integrity trailer");
            }
            if (crc32(data.data(), pos) != static_cast<std::uint32_t>(readI32(pos))) {
                throw std::runtime_error(path + ": checkpoint CRC mismatch, file is corrupt");
            }
            verified_ = true;
            break;
        }
        if (sections_.find(name) == sections_.end()) {
            order_.push_back(name);
        }
        sections_[name] = std::vector<std::uint8_t>(data.begin() + static_cast<std::ptrdiff_t>(pos),
                                                     data.begin() + static_cast<std::ptrdiff_t>(pos + length));
        pos += static_cast<std::size_t>(length);
    }
}

bool Checkpoint::Reader::section(const std::string& name, const StateReader& body) {
    auto it = sections_.find(name);
    if (it == sections_.end()) {
        return false;
    }
    BinReader r(it->second);
    body(r);
    if (r.remaining() > 0) {
        throw std::runtime_error("section '" + name + "': " + std::to_string(r.remaining())
                                  + " bytes left unread -- writer/reader format mismatch");
    }
    return true;
}

// ------------------------------------------------------------- Helpers

void Checkpoint::requireVersion(BinReader& in, int expected, const std::string& model) {
    int v = in.readByte();
    if (v != expected) {
        throw std::runtime_error(model + " state version " + std::to_string(v)
                                  + " not supported (this build reads version "
                                  + std::to_string(expected) + ")");
    }
}

void Checkpoint::writeDoubles(BinWriter& out, const std::vector<double>& a) {
    out.writeInt(static_cast<std::int32_t>(a.size()));
    for (double v : a) {
        out.writeDouble(v);
    }
}

void Checkpoint::readDoublesInto(BinReader& in, std::vector<double>& a) {
    std::int32_t n = in.readInt();
    if (n != static_cast<std::int32_t>(a.size())) {
        throw std::runtime_error("checkpoint array has " + std::to_string(n) + " entries, this instance has "
                                  + std::to_string(a.size()) + " -- incompatible configuration");
    }
    for (std::int32_t i = 0; i < n; i++) {
        a[static_cast<std::size_t>(i)] = in.readDouble();
    }
}

void Checkpoint::writeLongs(BinWriter& out, const std::vector<std::int64_t>& a) {
    out.writeInt(static_cast<std::int32_t>(a.size()));
    for (std::int64_t v : a) {
        out.writeLong(v);
    }
}

void Checkpoint::readLongsInto(BinReader& in, std::vector<std::int64_t>& a) {
    std::int32_t n = in.readInt();
    if (n != static_cast<std::int32_t>(a.size())) {
        throw std::runtime_error("checkpoint array has " + std::to_string(n) + " entries, this instance has "
                                  + std::to_string(a.size()) + " -- incompatible configuration");
    }
    for (std::int32_t i = 0; i < n; i++) {
        a[static_cast<std::size_t>(i)] = in.readLong();
    }
}

}  // namespace quantfinlib
