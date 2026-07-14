// Port of com.quantfinlib.fix.OrderCancelRequest (Java): typed view of a FIX
// OrderCancelRequest (35=F).
#pragma once

#include <cstdint>
#include <string>

#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct OrderCancelRequest {
    std::string clOrdId;
    std::string origClOrdId;
    std::string symbol;
    Side side;
    std::int64_t quantity;

    static OrderCancelRequest fromMessage(const FixMessage& m) {
        return OrderCancelRequest{
                m.getString(FixMessage::CL_ORD_ID),
                m.getString(FixMessage::ORIG_CL_ORD_ID),
                m.getString(FixMessage::SYMBOL),
                m.getChar(FixMessage::SIDE) == '1' ? Side::BUY : Side::SELL,
                m.getLong(FixMessage::ORDER_QTY)};
    }
};

}  // namespace quantfinlib
