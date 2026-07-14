// Port of com.quantfinlib.fix.NewOrderSingle (Java): typed view of a FIX
// NewOrderSingle (35=D). `price` is NaN for market orders (40=1); limit
// orders carry 40=2 and a price.
#pragma once

#include <cstdint>
#include <limits>
#include <string>

#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

struct NewOrderSingle {
    std::string clOrdId;
    std::string symbol;
    Side side;
    std::int64_t quantity;
    char ordType;
    double price;
    char timeInForce;

    static constexpr char ORD_TYPE_MARKET = '1';
    static constexpr char ORD_TYPE_LIMIT = '2';
    static constexpr char TIF_DAY = '0';
    static constexpr char TIF_IOC = '3';

    bool isMarket() const {
        return ordType == ORD_TYPE_MARKET;
    }

    static NewOrderSingle fromMessage(const FixMessage& m) {
        return NewOrderSingle{
                m.getString(FixMessage::CL_ORD_ID),
                m.getString(FixMessage::SYMBOL),
                m.getChar(FixMessage::SIDE) == '1' ? Side::BUY : Side::SELL,
                m.getLong(FixMessage::ORDER_QTY),
                m.getChar(FixMessage::ORD_TYPE),
                m.getDouble(FixMessage::PRICE, std::numeric_limits<double>::quiet_NaN()),
                m.has(FixMessage::TIME_IN_FORCE) ? m.getChar(FixMessage::TIME_IN_FORCE) : TIF_DAY};
    }
};

}  // namespace quantfinlib
