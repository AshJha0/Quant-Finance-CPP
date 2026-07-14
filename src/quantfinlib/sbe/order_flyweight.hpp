// Port of com.quantfinlib.sbe.OrderFlyweight (Java): SBE-style flyweight
// codec for an order-entry message -- the binary counterpart of a FIX
// NewOrderSingle, at fixed offsets with zero allocation and zero parsing
// (see TradeFlyweight for the pattern).
//
// Wire layout (little-endian, 44 bytes; 3 bytes padding keep the 64-bit
// fields naturally aligned):
//   offset  0  int32   messageType    = 2
//   offset  4  int64   orderId
//   offset 12  int32   symbolId
//   offset 16  int8    side           (0 = BUY, 1 = SELL)
//   offset 17  int8[3] padding
//   offset 20  int64   quantity
//   offset 28  double  price          (NaN = market order)
//   offset 36  int64   timestampNanos
#pragma once

#include <cstdint>

#include "quantfinlib/orderbook/side.hpp"
#include "quantfinlib/sbe/wire.hpp"

namespace quantfinlib {

class OrderFlyweight {
public:
    /** Message type discriminator at offset 0. */
    static constexpr int MESSAGE_TYPE = 2;
    /** Total encoded size in bytes. */
    static constexpr int BLOCK_LENGTH = 44;

    OrderFlyweight& wrap(unsigned char* buffer, int offset) {
        buffer_ = buffer;
        offset_ = offset;
        return *this;
    }

    /** Encodes a full order message at the wrap position (writes the type header). */
    OrderFlyweight& encode(std::int64_t orderId, int symbolId, Side side,
                           std::int64_t quantity, double price, std::int64_t timestampNanos) {
        sbe_wire::putI32(buffer_, offset_ + TYPE_OFFSET, MESSAGE_TYPE);
        sbe_wire::putI64(buffer_, offset_ + ORDER_ID_OFFSET, orderId);
        sbe_wire::putI32(buffer_, offset_ + SYMBOL_OFFSET, symbolId);
        sbe_wire::putI8(buffer_, offset_ + SIDE_OFFSET, side == Side::BUY ? 0 : 1);
        sbe_wire::putI64(buffer_, offset_ + QUANTITY_OFFSET, quantity);
        sbe_wire::putDouble(buffer_, offset_ + PRICE_OFFSET, price);
        sbe_wire::putI64(buffer_, offset_ + TIMESTAMP_OFFSET, timestampNanos);
        return *this;
    }

    std::int64_t orderId() const { return sbe_wire::getI64(buffer_, offset_ + ORDER_ID_OFFSET); }

    int symbolId() const { return sbe_wire::getI32(buffer_, offset_ + SYMBOL_OFFSET); }

    Side side() const {
        return sbe_wire::getI8(buffer_, offset_ + SIDE_OFFSET) == 0 ? Side::BUY : Side::SELL;
    }

    std::int64_t quantity() const { return sbe_wire::getI64(buffer_, offset_ + QUANTITY_OFFSET); }

    double price() const { return sbe_wire::getDouble(buffer_, offset_ + PRICE_OFFSET); }

    std::int64_t timestampNanos() const {
        return sbe_wire::getI64(buffer_, offset_ + TIMESTAMP_OFFSET);
    }

    static constexpr int TYPE_OFFSET = 0;
    static constexpr int ORDER_ID_OFFSET = 4;
    static constexpr int SYMBOL_OFFSET = 12;
    static constexpr int SIDE_OFFSET = 16;
    static constexpr int QUANTITY_OFFSET = 20;
    static constexpr int PRICE_OFFSET = 28;
    static constexpr int TIMESTAMP_OFFSET = 36;

private:
    unsigned char* buffer_ = nullptr;
    int offset_ = 0;
};

}  // namespace quantfinlib
