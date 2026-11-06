// Port of com.quantfinlib.sbe.QuoteFlyweight (Java): SBE-style flyweight
// codec for a two-sided quote message -- the outbound format of a market
// maker (and the inbound format of venue top-of-book feeds), completing
// the binary codec family: TradeFlyweight (trade in), OrderFlyweight
// (order out), quote (two-sided out).
//
// Wire layout (little-endian, 48 bytes):
//   offset  0  int32   messageType   = 3
//   offset  4  int32   symbolId          (dense id shared by both ends)
//   offset  8  double  bidPrice
//   offset 16  double  bidSize
//   offset 24  double  askPrice
//   offset 32  double  askSize
//   offset 40  int64   timestampNanos    (quote creation time)
//
// One-sided quotes carry NaN on the pulled side. Like its siblings, the
// flyweight holds only the wrap position: reuse a single instance for
// millions of messages, zero allocation per encode/decode.
#pragma once

#include <cstdint>

#include "quantfinlib/sbe/wire.hpp"

namespace quantfinlib {

class QuoteFlyweight {
public:
    /** Message type discriminator at offset 0. */
    static constexpr int MESSAGE_TYPE = 3;
    /** Total encoded size in bytes. */
    static constexpr int BLOCK_LENGTH = 48;

    static constexpr int TYPE_OFFSET = 0;
    static constexpr int SYMBOL_OFFSET = 4;
    static constexpr int BID_OFFSET = 8;
    static constexpr int BID_SIZE_OFFSET = 16;
    static constexpr int ASK_OFFSET = 24;
    static constexpr int ASK_SIZE_OFFSET = 32;
    static constexpr int TIMESTAMP_OFFSET = 40;

    /** Positions this flyweight over `buffer` at `offset`. */
    QuoteFlyweight& wrap(unsigned char* buffer, int offset) {
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

    /** Encodes a full quote message at the wrap position (writes the type header). */
    QuoteFlyweight& encode(int symbolId, double bidPrice, double bidSize, double askPrice,
                           double askSize, std::int64_t timestampNanos) {
        sbe_wire::putI32(buffer_, offset_ + TYPE_OFFSET, MESSAGE_TYPE);
        sbe_wire::putI32(buffer_, offset_ + SYMBOL_OFFSET, symbolId);
        sbe_wire::putDouble(buffer_, offset_ + BID_OFFSET, bidPrice);
        sbe_wire::putDouble(buffer_, offset_ + BID_SIZE_OFFSET, bidSize);
        sbe_wire::putDouble(buffer_, offset_ + ASK_OFFSET, askPrice);
        sbe_wire::putDouble(buffer_, offset_ + ASK_SIZE_OFFSET, askSize);
        sbe_wire::putI64(buffer_, offset_ + TIMESTAMP_OFFSET, timestampNanos);
        return *this;
    }

    int symbolId() const { return sbe_wire::getI32(buffer_, offset_ + SYMBOL_OFFSET); }

    double bidPrice() const { return sbe_wire::getDouble(buffer_, offset_ + BID_OFFSET); }

    double bidSize() const { return sbe_wire::getDouble(buffer_, offset_ + BID_SIZE_OFFSET); }

    double askPrice() const { return sbe_wire::getDouble(buffer_, offset_ + ASK_OFFSET); }

    double askSize() const { return sbe_wire::getDouble(buffer_, offset_ + ASK_SIZE_OFFSET); }

    std::int64_t timestampNanos() const {
        return sbe_wire::getI64(buffer_, offset_ + TIMESTAMP_OFFSET);
    }

    /** Reads the type discriminator without wrapping a flyweight. */
    static int typeAt(const unsigned char* buffer, int offset) {
        return sbe_wire::getI32(buffer, offset + TYPE_OFFSET);
    }

private:
    unsigned char* buffer_ = nullptr;
    int offset_ = 0;
};

}  // namespace quantfinlib
