// Port of com.quantfinlib.fix.OrderCancelReplaceRequest (Java): typed view of
// a FIX OrderCancelReplaceRequest (35=G) -- amend a working order's quantity
// and/or price. `price` is NaN for market (40=1).
#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/new_order_single.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct OrderCancelReplaceRequest {
    std::string clOrdId;
    std::string origClOrdId;
    std::string symbol;
    Side side;
    std::int64_t quantity;
    char ordType;
    double price;
    char timeInForce;

    bool isMarket() const {
        return ordType == NewOrderSingle::ORD_TYPE_MARKET;
    }

    static OrderCancelReplaceRequest fromMessage(const FixMessage& m) {
        return OrderCancelReplaceRequest{
                m.getString(FixMessage::CL_ORD_ID),
                m.getString(FixMessage::ORIG_CL_ORD_ID),
                m.getString(FixMessage::SYMBOL),
                m.getChar(FixMessage::SIDE) == '1' ? Side::BUY : Side::SELL,
                m.getLong(FixMessage::ORDER_QTY),
                m.getChar(FixMessage::ORD_TYPE),
                m.getDouble(FixMessage::PRICE, std::numeric_limits<double>::quiet_NaN()),
                m.has(FixMessage::TIME_IN_FORCE) ? m.getChar(FixMessage::TIME_IN_FORCE)
                                                  : NewOrderSingle::TIF_DAY};
    }
};

}  // namespace quantfinlib
