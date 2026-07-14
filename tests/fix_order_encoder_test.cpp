// Port of FixOrderEncoderTest.java: the garbage-free NewOrderSingle encoder,
// proven against the validated FixMessage codec -- every encoded message
// must parse through FixMessage::parse (which verifies BodyLength and
// CheckSum) with identical field values.
#include "quantfinlib/fix/fix_order_encoder.hpp"

#include <vector>

#include "qtest.hpp"
#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/orderbook/side.hpp"

using quantfinlib::FixMessage;
using quantfinlib::FixOrderEncoder;
using quantfinlib::Side;

namespace {

FixOrderEncoder encoder() {
    FixOrderEncoder enc("QFL", "VENUE", 16, 512);
    enc.registerSymbol(0, "EURUSD");
    enc.registerSymbol(1, "USDJPY");
    return enc;
}

FixMessage roundTrip(const FixOrderEncoder& enc) {
    std::vector<std::uint8_t> bytes(enc.buffer() + enc.offset(),
                                    enc.buffer() + enc.offset() + enc.length());
    return FixMessage::parse(bytes);
}

}  // namespace

TEST(limitOrderRoundTripsThroughTheValidatedParser) {
    FixOrderEncoder enc = encoder();
    std::int64_t ts = 1'800'000'000'000LL;  // 2027-01-15 08:00:00 UTC
    int len = enc.encodeLimit(42, 987654321L, 0, Side::BUY, 1'000'000, 108505, 5, ts);
    EXPECT_TRUE(len == enc.length());

    FixMessage m = roundTrip(enc);
    EXPECT_TRUE(m.getString(FixMessage::MSG_TYPE) == "D");
    EXPECT_TRUE(m.getString(FixMessage::SENDER_COMP_ID) == "QFL");
    EXPECT_TRUE(m.getString(FixMessage::TARGET_COMP_ID) == "VENUE");
    EXPECT_TRUE(m.getLong(FixMessage::MSG_SEQ_NUM) == 42);
    EXPECT_TRUE(m.getString(FixMessage::CL_ORD_ID) == "987654321");
    EXPECT_TRUE(m.getString(FixMessage::SYMBOL) == "EURUSD");
    EXPECT_TRUE(m.getString(FixMessage::SIDE) == "1");
    EXPECT_TRUE(m.getLong(FixMessage::ORDER_QTY) == 1'000'000);
    EXPECT_TRUE(m.getString(FixMessage::ORD_TYPE) == "2");     // limit
    EXPECT_TRUE(m.getString(FixMessage::PRICE) == "1.08505");  // exact rendering
    // Timestamp format: yyyyMMdd-HH:mm:ss.SSS.
    std::string sending = m.getString(FixMessage::SENDING_TIME);
    EXPECT_TRUE(sending.size() == 21 && sending[8] == '-' && sending[11] == ':' && sending[14] == ':'
                && sending[17] == '.');
    EXPECT_TRUE(sending == m.getString(FixMessage::TRANSACT_TIME));
}

TEST(marketOrderOmitsThePriceTag) {
    FixOrderEncoder enc = encoder();
    enc.encodeMarket(7, 1, 1, Side::SELL, 500'000, 1'800'000'000'000LL);
    FixMessage m = roundTrip(enc);
    EXPECT_TRUE(m.getString(FixMessage::ORD_TYPE) == "1");  // market
    EXPECT_TRUE(m.getString(FixMessage::SIDE) == "2");
    EXPECT_TRUE(m.getString(FixMessage::SYMBOL) == "USDJPY");
    EXPECT_TRUE(!m.has(FixMessage::PRICE));
}

TEST(scaledPricesRenderExactly) {
    FixOrderEncoder enc = encoder();
    // Sub-1 price: leading zero and full zero-padding of the fraction.
    enc.encodeLimit(1, 1, 0, Side::BUY, 1, 505, 5, 1'800'000'000'000LL);
    EXPECT_TRUE(roundTrip(enc).getString(FixMessage::PRICE) == "0.00505");
    // JPY-style 3 decimals.
    enc.encodeLimit(2, 2, 1, Side::BUY, 1, 155'123, 3, 1'800'000'000'000LL);
    EXPECT_TRUE(roundTrip(enc).getString(FixMessage::PRICE) == "155.123");
    // Zero decimals: plain integer.
    enc.encodeLimit(3, 3, 0, Side::BUY, 1, 99, 0, 1'800'000'000'000LL);
    EXPECT_TRUE(roundTrip(enc).getString(FixMessage::PRICE) == "99");
}

TEST(timestampDateCacheRollsAtUtcMidnight) {
    FixOrderEncoder enc = encoder();
    std::int64_t day = 86'400'000LL;
    std::int64_t midnightEve = 20'000LL * day - 1;  // 23:59:59.999
    enc.encodeLimit(1, 1, 0, Side::BUY, 1, 108505, 5, midnightEve);
    std::string before = roundTrip(enc).getString(FixMessage::SENDING_TIME);
    enc.encodeLimit(2, 2, 0, Side::BUY, 1, 108505, 5, midnightEve + 1);
    std::string after = roundTrip(enc).getString(FixMessage::SENDING_TIME);
    EXPECT_TRUE(before.size() >= 12 && before.substr(before.size() - 12) == "23:59:59.999");
    EXPECT_TRUE(after.size() >= 12 && after.substr(after.size() - 12) == "00:00:00.000");
    // The DATE part must have advanced with the rollover.
    EXPECT_TRUE(before.substr(0, 8) != after.substr(0, 8));
}

TEST(validation) {
    FixOrderEncoder enc = encoder();
    EXPECT_THROWS(enc.encodeLimit(1, 1, 9, Side::BUY, 1, 1, 1, 0));  // unregistered
    EXPECT_THROWS(FixOrderEncoder("", "V", 4, 512));
    EXPECT_THROWS(FixOrderEncoder("S", "V", 4, 64));  // buffer too small
}

QTEST_MAIN()
