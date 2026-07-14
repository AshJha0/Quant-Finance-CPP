#include "quantfinlib/data/tick_file_writer.hpp"

#include <bit>
#include <cstring>
#include <filesystem>
#include <stdexcept>

namespace quantfinlib {

namespace {

void putI32BE(std::ofstream& out, std::int32_t v) {
    unsigned char b[4];
    std::uint32_t u = static_cast<std::uint32_t>(v);
    b[0] = static_cast<unsigned char>(u >> 24);
    b[1] = static_cast<unsigned char>(u >> 16);
    b[2] = static_cast<unsigned char>(u >> 8);
    b[3] = static_cast<unsigned char>(u);
    out.write(reinterpret_cast<const char*>(b), 4);
}

void putU16BE(std::ofstream& out, int v) {
    unsigned char b[2];
    b[0] = static_cast<unsigned char>(static_cast<unsigned>(v) >> 8);
    b[1] = static_cast<unsigned char>(v);
    out.write(reinterpret_cast<const char*>(b), 2);
}

void putI64BE(std::ofstream& out, std::int64_t v) {
    unsigned char b[8];
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; i++) {
        b[i] = static_cast<unsigned char>(u >> (8 * (7 - i)));
    }
    out.write(reinterpret_cast<const char*>(b), 8);
}

void putDoubleBE(std::ofstream& out, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    putI64BE(out, static_cast<std::int64_t>(bits));
}

}  // namespace

TickFileWriter::TickFileWriter(const std::string& path) : defined_(64, false) {
    std::filesystem::path p(path);
    if (p.has_parent_path()) {
        std::filesystem::create_directories(p.parent_path());
    }
    out_.open(path, std::ios::binary | std::ios::trunc);
    if (!out_) {
        throw std::runtime_error("cannot open tick file for writing: " + path);
    }
    putI32BE(out_, MAGIC);
    out_.put(static_cast<char>(VERSION));
}

TickFileWriter::~TickFileWriter() {
    close();
}

void TickFileWriter::defineSymbol(int symbolId, const std::string& symbol) {
    if (static_cast<std::size_t>(symbolId) >= defined_.size()) {
        std::size_t newSize = static_cast<std::size_t>(
                std::bit_floor(static_cast<unsigned>(symbolId))) * 2;
        defined_.resize(newSize, false);
    }
    if (defined_[static_cast<std::size_t>(symbolId)]) {
        return;
    }
    out_.put(static_cast<char>(TYPE_SYMBOL));
    putI32BE(out_, symbolId);
    putU16BE(out_, static_cast<int>(symbol.size()));
    out_.write(symbol.data(), static_cast<std::streamsize>(symbol.size()));
    defined_[static_cast<std::size_t>(symbolId)] = true;
}

void TickFileWriter::write(int symbolId, double price, double size, std::int64_t timestampNanos) {
    if (static_cast<std::size_t>(symbolId) >= defined_.size()
            || !defined_[static_cast<std::size_t>(symbolId)]) {
        throw std::runtime_error("symbol id " + std::to_string(symbolId)
                                  + " not defined before first tick");
    }
    out_.put(static_cast<char>(TYPE_TICK));
    putI32BE(out_, symbolId);
    putDoubleBE(out_, price);
    putDoubleBE(out_, size);
    putI64BE(out_, timestampNanos);
    tickCount_++;
}

void TickFileWriter::close() {
    if (out_.is_open()) {
        out_.close();
    }
}

}  // namespace quantfinlib
