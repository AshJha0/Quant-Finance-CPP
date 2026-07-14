// Shared little-endian wire primitives for the sbe:: flyweight family
// (OrderFlyweight, TradeFlyweight, QuoteFlyweight). Java's ByteBuffer is
// told .order(LITTLE_ENDIAN) once at allocation and every flyweight then
// just calls putInt/putLong/putDouble/get*; C++ has no such buffer mode,
// so the explicit byte-at-a-time encode/decode lives here exactly once
// (the same "shared once" rationale as book_primitives.hpp) rather than
// being duplicated in each flyweight. Never reinterpret_cast the buffer:
// doubles cross via std::memcpy into/out of a uint64_t so the bit pattern
// is portable and there is no strict-aliasing UB.
#pragma once

#include <cstdint>
#include <cstring>

namespace quantfinlib {
namespace sbe_wire {

inline void putI8(unsigned char* buf, int off, std::int8_t v) {
    buf[off] = static_cast<unsigned char>(v);
}

inline std::int8_t getI8(const unsigned char* buf, int off) {
    return static_cast<std::int8_t>(buf[off]);
}

inline void putI32(unsigned char* buf, int off, std::int32_t v) {
    std::uint32_t u = static_cast<std::uint32_t>(v);
    for (int i = 0; i < 4; i++) {
        buf[off + i] = static_cast<unsigned char>(u >> (8 * i));
    }
}

inline std::int32_t getI32(const unsigned char* buf, int off) {
    std::uint32_t u = 0;
    for (int i = 0; i < 4; i++) {
        u |= static_cast<std::uint32_t>(buf[off + i]) << (8 * i);
    }
    return static_cast<std::int32_t>(u);
}

inline void putI64(unsigned char* buf, int off, std::int64_t v) {
    std::uint64_t u = static_cast<std::uint64_t>(v);
    for (int i = 0; i < 8; i++) {
        buf[off + i] = static_cast<unsigned char>(u >> (8 * i));
    }
}

inline std::int64_t getI64(const unsigned char* buf, int off) {
    std::uint64_t u = 0;
    for (int i = 0; i < 8; i++) {
        u |= static_cast<std::uint64_t>(buf[off + i]) << (8 * i);
    }
    return static_cast<std::int64_t>(u);
}

inline void putDouble(unsigned char* buf, int off, double v) {
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof(bits));
    putI64(buf, off, static_cast<std::int64_t>(bits));
}

inline double getDouble(const unsigned char* buf, int off) {
    std::uint64_t bits = static_cast<std::uint64_t>(getI64(buf, off));
    double v;
    std::memcpy(&v, &bits, sizeof(v));
    return v;
}

}  // namespace sbe_wire
}  // namespace quantfinlib
