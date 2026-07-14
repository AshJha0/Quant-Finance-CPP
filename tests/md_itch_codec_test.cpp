// Port of ItchCodecTest (Java): encode/decode round trips for every
// message type in the subset, packStock/unpackStock truncation and
// padding, non-zero-offset framing, and the unknown-type length pin.
#include "quantfinlib/marketdata/itch_codec.hpp"

#include "qtest.hpp"

using quantfinlib::ItchCodec;

TEST(addOrderRoundTrips) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    std::int64_t stock = ItchCodec::packStock("AAPL");
    int len = ItchCodec::encodeAdd(buf, 0, 42, 34200000000000LL, 987654321L, ItchCodec::BUY, 300,
                                   stock, 1755000);
    EXPECT_TRUE(len == 36);
    EXPECT_TRUE(len == ItchCodec::length(ItchCodec::ADD));
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::ADD);
    EXPECT_TRUE(v.stockLocate() == 42);
    EXPECT_TRUE(v.timestampNanos() == 34200000000000LL);
    EXPECT_TRUE(v.orderRef() == 987654321L);
    EXPECT_TRUE(v.side() == ItchCodec::BUY);
    EXPECT_TRUE(v.shares() == 300);
    EXPECT_TRUE(v.stock() == stock);
    EXPECT_TRUE(v.priceTick() == 1755000);
}

TEST(executedRoundTrips) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    int len = ItchCodec::encodeExecuted(buf, 0, 7, 123L, 55L, 200, 999L);
    EXPECT_TRUE(len == 31);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::EXECUTED);
    EXPECT_TRUE(v.orderRef() == 55L);
    EXPECT_TRUE(v.deltaShares() == 200);
    EXPECT_TRUE(v.matchNumber() == 999L);
}

TEST(cancelAndDeleteRoundTrip) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    EXPECT_TRUE(ItchCodec::encodeCancel(buf, 0, 7, 1L, 55L, 40) == 23);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::CANCEL);
    EXPECT_TRUE(v.orderRef() == 55L);
    EXPECT_TRUE(v.deltaShares() == 40);

    EXPECT_TRUE(ItchCodec::encodeDelete(buf, 0, 7, 2L, 66L) == 19);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::DELETE);
    EXPECT_TRUE(v.orderRef() == 66L);
}

TEST(replaceRoundTrips) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    int len = ItchCodec::encodeReplace(buf, 0, 7, 3L, 55L, 56L, 150, 1760000);
    EXPECT_TRUE(len == 35);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::REPLACE);
    EXPECT_TRUE(v.origRef() == 55L);
    EXPECT_TRUE(v.newRef() == 56L);
    EXPECT_TRUE(v.shares() == 150);
    EXPECT_TRUE(v.priceTick() == 1760000);
}

TEST(tradeRoundTrips) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    std::int64_t stock = ItchCodec::packStock("MSFT");
    int len = ItchCodec::encodeTrade(buf, 0, 9, 4L, 77L, ItchCodec::SELL, 500, stock, 4101500,
                                     12345L);
    EXPECT_TRUE(len == 44);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.type() == ItchCodec::TRADE);
    EXPECT_TRUE(v.orderRef() == 77L);
    EXPECT_TRUE(v.side() == ItchCodec::SELL);
    EXPECT_TRUE(v.shares() == 500);
    EXPECT_TRUE(v.stock() == stock);
    EXPECT_TRUE(v.priceTick() == 4101500);
    EXPECT_TRUE(v.matchNumber() == 12345L);
}

TEST(stockPackingHandlesShortAndFullSymbols) {
    EXPECT_TRUE(ItchCodec::unpackStock(ItchCodec::packStock("A")) == "A");
    EXPECT_TRUE(ItchCodec::unpackStock(ItchCodec::packStock("BRKB")) == "BRKB");
    EXPECT_TRUE(ItchCodec::unpackStock(ItchCodec::packStock("ABCDEFGH")) == "ABCDEFGH");
}

TEST(messagesEncodeAtNonZeroOffsets) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    int off = 13;
    ItchCodec::encodeDelete(buf, off, 7, 5L, 88L);
    v.wrap(buf, off);
    EXPECT_TRUE(v.type() == ItchCodec::DELETE);
    EXPECT_TRUE(v.orderRef() == 88L);
}

TEST(unknownTypeHasNegativeLength) {
    EXPECT_TRUE(ItchCodec::length(static_cast<std::uint8_t>('Z')) == -1);
}

// Price-ceiling documentation pin: a price at the signed-int32 boundary
// round-trips; the class doc's $214,748.36 ceiling is exactly INT32_MAX
// ticks (0.0001 each) -- the wire value one tick past that (2^31, still a
// perfectly valid *unsigned* ITCH price) decodes negative through this
// library's signed-int pipeline, exactly as documented.
TEST(priceCeilingAtInt32MaxRoundTripsThenWrapsNegativeBeyondIt) {
    unsigned char buf[64] = {};
    ItchCodec::View v;
    std::int64_t stock = ItchCodec::packStock("BRKA");
    ItchCodec::encodeAdd(buf, 0, 1, 1L, 1L, ItchCodec::BUY, 100, stock, INT32_MAX);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.priceTick() == INT32_MAX);

    // The unsigned wire value 2^31 (INT32_MAX + 1) decodes as INT32_MIN
    // through the documented signed-int reinterpretation.
    ItchCodec::encodeAdd(buf, 0, 1, 1L, 1L, ItchCodec::BUY, 100, stock, INT32_MIN);
    v.wrap(buf, 0);
    EXPECT_TRUE(v.priceTick() == INT32_MIN);
}

QTEST_MAIN()
