#include "quantfinlib/data/tick_file_reader.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

#include "quantfinlib/data/tick_file_writer.hpp"

namespace quantfinlib {

namespace {

void requireRead(std::ifstream& in, char* buf, std::streamsize n, const std::string& path) {
    in.read(buf, n);
    if (!in) {
        throw std::runtime_error("truncated QFLT file: " + path);
    }
}

std::int32_t readI32BE(std::ifstream& in, const std::string& path) {
    unsigned char b[4];
    requireRead(in, reinterpret_cast<char*>(b), 4, path);
    std::uint32_t u = (static_cast<std::uint32_t>(b[0]) << 24) | (static_cast<std::uint32_t>(b[1]) << 16)
            | (static_cast<std::uint32_t>(b[2]) << 8) | static_cast<std::uint32_t>(b[3]);
    return static_cast<std::int32_t>(u);
}

int readU16BE(std::ifstream& in, const std::string& path) {
    unsigned char b[2];
    requireRead(in, reinterpret_cast<char*>(b), 2, path);
    return (static_cast<int>(b[0]) << 8) | static_cast<int>(b[1]);
}

std::int64_t readI64BE(std::ifstream& in, const std::string& path) {
    unsigned char b[8];
    requireRead(in, reinterpret_cast<char*>(b), 8, path);
    std::uint64_t u = 0;
    for (int i = 0; i < 8; i++) {
        u = (u << 8) | b[i];
    }
    return static_cast<std::int64_t>(u);
}

double readDoubleBE(std::ifstream& in, const std::string& path) {
    std::int64_t bits = readI64BE(in, path);
    std::uint64_t u = static_cast<std::uint64_t>(bits);
    double v;
    std::memcpy(&v, &u, sizeof(v));
    return v;
}

}  // namespace

std::int64_t TickFileReader::replay(const std::string& path, ReplayHandler& handler) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot open QFLT tick file: " + path);
    }
    std::int32_t magic = readI32BE(in, path);
    if (magic != TickFileWriter::MAGIC) {
        throw std::runtime_error("not a QFLT tick file: " + path);
    }
    int version = in.get();
    if (version < 0) {
        throw std::runtime_error("truncated QFLT file: " + path);
    }
    if (static_cast<std::uint8_t>(version) != TickFileWriter::VERSION) {
        throw std::runtime_error("unsupported QFLT version " + std::to_string(version));
    }
    std::int64_t ticks = 0;
    while (true) {
        int type = in.get();
        if (type < 0) {
            return ticks;   // clean EOF
        }
        if (type == TickFileWriter::TYPE_SYMBOL) {
            std::int32_t id = readI32BE(in, path);
            int len = readU16BE(in, path);
            std::string name(static_cast<std::size_t>(len), '\0');
            if (len > 0) {
                requireRead(in, name.data(), len, path);
            }
            handler.onSymbol(id, name);
        } else if (type == TickFileWriter::TYPE_TICK) {
            std::int32_t id = readI32BE(in, path);
            double price = readDoubleBE(in, path);
            double size = readDoubleBE(in, path);
            std::int64_t ts = readI64BE(in, path);
            handler.onTick(id, price, size, ts);
            ticks++;
        } else {
            throw std::runtime_error("corrupt record type " + std::to_string(type) + " after "
                                      + std::to_string(ticks) + " ticks");
        }
    }
}

std::int64_t TickFileReader::replayPaced(const std::string& path, ReplayHandler& handler,
                                         double speedMultiplier) {
    if (speedMultiplier <= 0) {
        throw std::invalid_argument("speedMultiplier must be positive");
    }
    class PacedHandler : public ReplayHandler {
    public:
        PacedHandler(ReplayHandler& inner, double speed) : inner_(inner), speed_(speed) {}

        void onSymbol(int symbolId, const std::string& symbol) override {
            inner_.onSymbol(symbolId, symbol);
        }

        void onTick(int symbolId, double price, double size, std::int64_t timestampNanos) override {
            if (!first_) {
                std::int64_t gap = static_cast<std::int64_t>((timestampNanos - prevTs_) / speed_);
                if (gap > 0) {
                    std::this_thread::sleep_for(
                            std::chrono::nanoseconds(std::min<std::int64_t>(gap, 10'000'000'000LL)));
                }
            }
            prevTs_ = timestampNanos;
            first_ = false;
            inner_.onTick(symbolId, price, size, timestampNanos);
        }

    private:
        ReplayHandler& inner_;
        double speed_;
        std::int64_t prevTs_ = 0;
        bool first_ = true;
    };

    PacedHandler paced(handler, speedMultiplier);
    return replay(path, paced);
}

}  // namespace quantfinlib
