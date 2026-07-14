// Port of FixMessageTest.java: codec round trips + checksum pins, plus the
// FixDecoder framing tests (partial reads, corrupt BodyLength resync) that
// the Java suite keeps in the same file.
#include "quantfinlib/fix/fix_decoder.hpp"
#include "quantfinlib/fix/fix_message.hpp"

#include <cstdio>
#include <string>
#include <vector>

#include "qtest.hpp"

using quantfinlib::FixDecoder;
using quantfinlib::FixMessage;

namespace {

std::vector<std::uint8_t> sampleOrder() {
    return FixMessage::builder(FixMessage::NEW_ORDER_SINGLE)
            .field(FixMessage::CL_ORD_ID, std::string("ord-1"))
            .field(FixMessage::SYMBOL, std::string("EURUSD"))
            .field(FixMessage::SIDE, '1')
            .field(FixMessage::ORDER_QTY, static_cast<std::int64_t>(1'000'000))
            .field(FixMessage::ORD_TYPE, '2')
            .field(FixMessage::PRICE, 1.0851)
            .encode("CLIENT", "VENUE", 7, "20260704-12:00:00.000");
}

}  // namespace

TEST(corruptBodyLengthFailsLoudlyInsteadOfZombifying) {
    // One flipped byte in the BodyLength digits used to inflate the
    // expected frame size, and the framer would then wait FOREVER -- a
    // zombie session silently swallowing all later traffic.
    std::vector<std::uint8_t> bytes = sampleOrder();
    std::size_t soh = 0;
    while (bytes[soh] != static_cast<std::uint8_t>(FixMessage::SOH)) {
        ++soh;
    }
    bytes[soh + 3] = ':';  // '9' + 1: the classic bit flip
    FixDecoder decoder;
    decoder.feed(bytes.data(), 0, bytes.size());
    EXPECT_THROWS(decoder.poll());
}

TEST(encodeParseRoundTrips) {
    FixMessage m = FixMessage::parse(sampleOrder());
    EXPECT_TRUE(m.msgType() == "D");
    EXPECT_TRUE(m.getString(FixMessage::CL_ORD_ID) == "ord-1");
    EXPECT_TRUE(m.getString(FixMessage::SYMBOL) == "EURUSD");
    EXPECT_TRUE(m.getChar(FixMessage::SIDE) == '1');
    EXPECT_TRUE(m.getLong(FixMessage::ORDER_QTY) == 1'000'000);
    EXPECT_NEAR(m.getDouble(FixMessage::PRICE), 1.0851, 0.0);
    EXPECT_TRUE(m.getString(FixMessage::SENDER_COMP_ID) == "CLIENT");
    EXPECT_TRUE(m.getString(FixMessage::TARGET_COMP_ID) == "VENUE");
    EXPECT_TRUE(m.getLong(FixMessage::MSG_SEQ_NUM) == 7);
}

TEST(checksumIsIndependentlyVerifiable) {
    std::vector<std::uint8_t> bytes = sampleOrder();
    std::string raw(bytes.begin(), bytes.end());
    std::string marker = std::string(1, FixMessage::SOH) + "10=";
    std::size_t checksumField = raw.rfind(marker);
    unsigned sum = 0;
    for (std::size_t i = 0; i <= checksumField; ++i) {
        sum += static_cast<unsigned char>(bytes[i]);
    }
    char expected[4];
    std::snprintf(expected, sizeof(expected), "%03u", sum % 256);
    std::string declared = raw.substr(checksumField + 4, raw.size() - 1 - (checksumField + 4));
    EXPECT_TRUE(std::string(expected) == declared);
}

TEST(tamperedMessagesAreRejected) {
    std::vector<std::uint8_t> bytes = sampleOrder();
    bytes[30] ^= 0x01;  // flip one payload bit
    EXPECT_THROWS(FixMessage::parse(bytes));

    // Wrong protocol prefix.
    std::string bad = std::string("8=FIX.4.2") + FixMessage::SOH + "9=5" + FixMessage::SOH
                       + "35=0" + FixMessage::SOH + "10=000" + FixMessage::SOH;
    std::vector<std::uint8_t> badBytes(bad.begin(), bad.end());
    EXPECT_THROWS(FixMessage::parse(badBytes));
}

TEST(doublesRenderAsPlainDecimals) {
    std::vector<std::uint8_t> bytes = FixMessage::builder("0")
                                               .field(FixMessage::PRICE, 0.0001)
                                               .encode("A", "B", 1, "20260704-12:00:00.000");
    std::string raw(bytes.begin(), bytes.end());
    std::string needle = std::string("44=0.0001") + FixMessage::SOH;
    EXPECT_TRUE(raw.find(needle) != std::string::npos);
}

TEST(decoderReassemblesFragmentsAndSplitsCoalescedMessages) {
    std::vector<std::uint8_t> first = sampleOrder();
    std::vector<std::uint8_t> second = FixMessage::builder(FixMessage::HEARTBEAT)
                                                .encode("CLIENT", "VENUE", 8, "20260704-12:00:01.000");
    std::vector<std::uint8_t> stream;
    stream.insert(stream.end(), first.begin(), first.end());
    stream.insert(stream.end(), second.begin(), second.end());

    FixDecoder decoder;
    int produced = 0;
    // Worst case: one byte at a time.
    for (std::uint8_t b : stream) {
        decoder.feed(&b, 0, 1);
        std::optional<FixMessage> m;
        while ((m = decoder.poll()).has_value()) {
            ++produced;
            if (produced == 1) {
                EXPECT_TRUE(m->msgType() == "D");
            } else {
                EXPECT_TRUE(m->msgType() == "0");
            }
        }
    }
    EXPECT_TRUE(produced == 2);
    EXPECT_TRUE(!decoder.poll().has_value());
}

QTEST_MAIN()
