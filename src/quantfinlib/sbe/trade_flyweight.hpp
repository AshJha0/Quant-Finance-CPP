// Port of com.quantfinlib.sbe.TradeFlyweight (Java): SBE-style flyweight
// codec for a market-data trade message: fixed field offsets over a raw
// buffer, so encode/decode is a handful of absolute primitive reads/writes
// -- zero allocation, zero parsing, zero copying. This is the wire format
// real HFT feeds use (ITCH/SBE family), as opposed to the text protocols
// (JSON/FIX tag-value) of the retail edges.
//
// Wire layout (little-endian, 32 bytes):
//   offset  0  int32   messageType   = 1
//   offset  4  int32   symbolId          (dense id shared by both ends)
//   offset  8  double  price
//   offset 16  double  size
//   offset 24  int64   timestampNanos    (exchange event time)
//
// Usage: wrap(buffer, offset) then read/write fields. The flyweight holds
// no state besides the wrap position -- reuse one instance for millions
// of messages.
#pragma once

#include <cstdint>

#include "quantfinlib/sbe/wire.hpp"

namespace quantfinlib {

class TradeFlyweight {
public:
    /** Message type discriminator at offset 0. */
    static constexpr int MESSAGE_TYPE = 1;
    /** Total encoded size in bytes. */
    static constexpr int BLOCK_LENGTH = 32;

    static constexpr int TYPE_OFFSET = 0;
    static constexpr int SYMBOL_OFFSET = 4;
    static constexpr int PRICE_OFFSET = 8;
    static constexpr int SIZE_OFFSET = 16;
    static constexpr int TIMESTAMP_OFFSET = 24;

    /** Positions this flyweight over `buffer` at `offset`. */
    TradeFlyweight& wrap(unsigned char* buffer, int offset) {
        buffer_ = buffer;
        offset_ = offset;
        return *this;
    }

    /**
     * Checked wrap for wire bytes: `available` bytes follow `offset`. Returns
     * false, leaving the flyweight untouched, unless a whole message of this
     * type with valid enum bytes is there.
     */
    bool wrap(unsigned char* buffer, int offset, int available) {
        if (buffer == nullptr || offset < 0 || available < BLOCK_LENGTH
            || sbe_wire::getI32(buffer, offset + TYPE_OFFSET) != MESSAGE_TYPE) {
            return false;
        }
        buffer_ = buffer;
        offset_ = offset;
        return true;
    }

    /** Encodes a full trade message at the wrap position (writes the type header). */
    TradeFlyweight& encode(int symbolId, double price, double size, std::int64_t timestampNanos) {
        sbe_wire::putI32(buffer_, offset_ + TYPE_OFFSET, MESSAGE_TYPE);
        sbe_wire::putI32(buffer_, offset_ + SYMBOL_OFFSET, symbolId);
        sbe_wire::putDouble(buffer_, offset_ + PRICE_OFFSET, price);
        sbe_wire::putDouble(buffer_, offset_ + SIZE_OFFSET, size);
        sbe_wire::putI64(buffer_, offset_ + TIMESTAMP_OFFSET, timestampNanos);
        return *this;
    }

    int symbolId() const { return sbe_wire::getI32(buffer_, offset_ + SYMBOL_OFFSET); }

    double price() const { return sbe_wire::getDouble(buffer_, offset_ + PRICE_OFFSET); }

    double size() const { return sbe_wire::getDouble(buffer_, offset_ + SIZE_OFFSET); }

    std::int64_t timestampNanos() const {
        return sbe_wire::getI64(buffer_, offset_ + TIMESTAMP_OFFSET);
    }

    /** Reads the message-type discriminator at `offset` without wrapping. */
    static int typeAt(const unsigned char* buffer, int offset) {
        return sbe_wire::getI32(buffer, offset + TYPE_OFFSET);
    }

private:
    unsigned char* buffer_ = nullptr;
    int offset_ = 0;
};

}  // namespace quantfinlib
