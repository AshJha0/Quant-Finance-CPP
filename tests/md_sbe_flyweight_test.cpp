// Port of QuoteFlyweightTest (Java, plus offset pins for OrderFlyweight
// and TradeFlyweight): field round-trips at arbitrary offsets, NaN
// one-sided quotes, type discrimination against the sibling codecs, and
// an offsets test asserting every flyweight field offset equals the Java
// constant (the byte-for-byte wire-compatibility pin this whole package
// exists for). The Java allocation-free-loop tests have no C++ analogue
// (no ThreadMXBean) and are not ported.
#include "quantfinlib/sbe/order_flyweight.hpp"
#include "quantfinlib/sbe/quote_flyweight.hpp"
#include "quantfinlib/sbe/trade_flyweight.hpp"

#include <cmath>

#include "qtest.hpp"

using quantfinlib::OrderFlyweight;
using quantfinlib::QuoteFlyweight;
using quantfinlib::Side;
using quantfinlib::TradeFlyweight;

// ------------------------------------------------------------------
// Offsets: byte-for-byte parity with the Java constants.
// ------------------------------------------------------------------

TEST(orderFlyweightOffsetsMatchJava) {
    EXPECT_TRUE(OrderFlyweight::MESSAGE_TYPE == 2);
    EXPECT_TRUE(OrderFlyweight::BLOCK_LENGTH == 44);
    EXPECT_TRUE(OrderFlyweight::TYPE_OFFSET == 0);
    EXPECT_TRUE(OrderFlyweight::ORDER_ID_OFFSET == 4);
    EXPECT_TRUE(OrderFlyweight::SYMBOL_OFFSET == 12);
    EXPECT_TRUE(OrderFlyweight::SIDE_OFFSET == 16);
    EXPECT_TRUE(OrderFlyweight::QUANTITY_OFFSET == 20);
    EXPECT_TRUE(OrderFlyweight::PRICE_OFFSET == 28);
    EXPECT_TRUE(OrderFlyweight::TIMESTAMP_OFFSET == 36);
}

TEST(tradeFlyweightOffsetsMatchJava) {
    EXPECT_TRUE(TradeFlyweight::MESSAGE_TYPE == 1);
    EXPECT_TRUE(TradeFlyweight::BLOCK_LENGTH == 32);
    EXPECT_TRUE(TradeFlyweight::TYPE_OFFSET == 0);
    EXPECT_TRUE(TradeFlyweight::SYMBOL_OFFSET == 4);
    EXPECT_TRUE(TradeFlyweight::PRICE_OFFSET == 8);
    EXPECT_TRUE(TradeFlyweight::SIZE_OFFSET == 16);
    EXPECT_TRUE(TradeFlyweight::TIMESTAMP_OFFSET == 24);
}

TEST(quoteFlyweightOffsetsMatchJava) {
    EXPECT_TRUE(QuoteFlyweight::MESSAGE_TYPE == 3);
    EXPECT_TRUE(QuoteFlyweight::BLOCK_LENGTH == 48);
    EXPECT_TRUE(QuoteFlyweight::TYPE_OFFSET == 0);
    EXPECT_TRUE(QuoteFlyweight::SYMBOL_OFFSET == 4);
    EXPECT_TRUE(QuoteFlyweight::BID_OFFSET == 8);
    EXPECT_TRUE(QuoteFlyweight::BID_SIZE_OFFSET == 16);
    EXPECT_TRUE(QuoteFlyweight::ASK_OFFSET == 24);
    EXPECT_TRUE(QuoteFlyweight::ASK_SIZE_OFFSET == 32);
    EXPECT_TRUE(QuoteFlyweight::TIMESTAMP_OFFSET == 40);
}

// ------------------------------------------------------------------
// Round trips
// ------------------------------------------------------------------

TEST(orderFlyweightRoundTripsAllFieldsAtAnOffset) {
    unsigned char buf[256] = {};
    OrderFlyweight order;
    order.wrap(buf, 64).encode(123456789L, 7, Side::SELL, 300, 1.08505, 987654321L);

    OrderFlyweight reader;
    reader.wrap(buf, 64);
    EXPECT_TRUE(reader.orderId() == 123456789L);
    EXPECT_TRUE(reader.symbolId() == 7);
    EXPECT_TRUE(reader.side() == Side::SELL);
    EXPECT_TRUE(reader.quantity() == 300);
    EXPECT_NEAR(reader.price(), 1.08505, 1e-12);
    EXPECT_TRUE(reader.timestampNanos() == 987654321L);
}

TEST(orderFlyweightBuySideAndNanMarketPrice) {
    unsigned char buf[64] = {};
    OrderFlyweight order;
    order.wrap(buf, 0).encode(1, 1, Side::BUY, 100, std::nan(""), 1L);
    OrderFlyweight reader;
    reader.wrap(buf, 0);
    EXPECT_TRUE(reader.side() == Side::BUY);
    EXPECT_TRUE(std::isnan(reader.price()));
}

TEST(tradeFlyweightRoundTripsAllFields) {
    unsigned char buf[64] = {};
    TradeFlyweight trade;
    trade.wrap(buf, 0).encode(9, 1.0852, 250000, 42L);
    TradeFlyweight reader;
    reader.wrap(buf, 0);
    EXPECT_TRUE(reader.symbolId() == 9);
    EXPECT_NEAR(reader.price(), 1.0852, 1e-12);
    EXPECT_NEAR(reader.size(), 250000, 1e-9);
    EXPECT_TRUE(reader.timestampNanos() == 42L);
    EXPECT_TRUE(TradeFlyweight::typeAt(buf, 0) == TradeFlyweight::MESSAGE_TYPE);
}

TEST(quoteFlyweightRoundTripsAllFieldsAtAnOffset) {
    unsigned char buf[256] = {};
    QuoteFlyweight quote;
    quote.wrap(buf, 64).encode(7, 1.08505, 2000000, 1.08515, 500000, 123456789L);

    QuoteFlyweight reader;
    reader.wrap(buf, 64);
    EXPECT_TRUE(reader.symbolId() == 7);
    EXPECT_NEAR(reader.bidPrice(), 1.08505, 1e-12);
    EXPECT_NEAR(reader.bidSize(), 2000000, 1e-9);
    EXPECT_NEAR(reader.askPrice(), 1.08515, 1e-12);
    EXPECT_NEAR(reader.askSize(), 500000, 1e-9);
    EXPECT_TRUE(reader.timestampNanos() == 123456789L);
    EXPECT_TRUE(QuoteFlyweight::typeAt(buf, 64) == QuoteFlyweight::MESSAGE_TYPE);
    EXPECT_TRUE(QuoteFlyweight::BLOCK_LENGTH == 48);
}

TEST(quoteFlyweightOneSidedQuotesCarryNaN) {
    unsigned char buf[64] = {};
    QuoteFlyweight quote;
    quote.wrap(buf, 0).encode(1, std::nan(""), 0, 1.0852, 1000000, 1L);
    EXPECT_TRUE(std::isnan(quote.bidPrice()));
    EXPECT_NEAR(quote.askPrice(), 1.0852, 1e-12);
}

TEST(typeDiscriminatesFromSiblingCodecs) {
    unsigned char buf[256] = {};
    TradeFlyweight().wrap(buf, 0).encode(1, 1.0, 1, 1L);
    OrderFlyweight().wrap(buf, 64).encode(1L, 1, Side::BUY, 100, 1.0, 1L);
    QuoteFlyweight().wrap(buf, 128).encode(1, 1.0, 1, 1.1, 1, 1L);
    // The three message types are distinct on the wire.
    EXPECT_TRUE(QuoteFlyweight::typeAt(buf, 0) == TradeFlyweight::MESSAGE_TYPE);
    EXPECT_TRUE(QuoteFlyweight::typeAt(buf, 64) == OrderFlyweight::MESSAGE_TYPE);
    EXPECT_TRUE(QuoteFlyweight::typeAt(buf, 128) == QuoteFlyweight::MESSAGE_TYPE);
    EXPECT_TRUE(TradeFlyweight::MESSAGE_TYPE != QuoteFlyweight::MESSAGE_TYPE
                && OrderFlyweight::MESSAGE_TYPE != QuoteFlyweight::MESSAGE_TYPE);
}

QTEST_MAIN()
