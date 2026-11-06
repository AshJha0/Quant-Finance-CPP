// Regression tests for the 2026-11-06 review, finding C11: every decoder
// that meets untrusted bytes stays inside them and rejects what it cannot
// represent. Buffers are heap-allocated to size so ASan catches any overread.
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/fix/fix_decoder.hpp"
#include "quantfinlib/fix/fix_exec_report_view.hpp"
#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/fix_parse.hpp"
#include "quantfinlib/sbe/order_flyweight.hpp"
#include "quantfinlib/sbe/quote_flyweight.hpp"
#include "quantfinlib/sbe/trade_flyweight.hpp"

using quantfinlib::FixDecoder;
using quantfinlib::FixExecReportView;
using quantfinlib::FixMessage;
using quantfinlib::OrderFlyweight;
using quantfinlib::QuoteFlyweight;
using quantfinlib::Side;
using quantfinlib::TradeFlyweight;
namespace fp = quantfinlib::fix_parse;

namespace {

template <typename Fn>
bool throwsInvalid(Fn fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

template <typename Fn>
bool throwsCorrupt(Fn fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return false;
    } catch (const std::runtime_error&) {
        return true;
    }
    return false;
}

const std::uint8_t* bytes(const std::string& s) {
    return reinterpret_cast<const std::uint8_t*>(s.data());
}

/** Exact-size heap copy, so a one-byte overread trips ASan. */
std::unique_ptr<std::uint8_t[]> exact(const std::string& s) {
    auto p = std::make_unique<std::uint8_t[]>(s.size());
    std::memcpy(p.get(), s.data(), s.size());
    return p;
}

std::vector<std::uint8_t> message(const FixMessage::Builder& b) {
    return b.encode("A", "B", 1, "20261106-10:00:00.000");
}

/** Re-frames raw field text with a correct BodyLength and CheckSum. */
std::vector<std::uint8_t> frame(const std::string& afterLength) {
    std::string payload = std::string("8=FIX.4.4\x01") + "9=" + std::to_string(afterLength.size())
                          + "\x01" + afterLength;
    unsigned sum = 0;
    for (unsigned char c : payload) {
        sum += c;
    }
    char tail[12];
    std::snprintf(tail, sizeof(tail), "10=%03u\x01", sum % 256);
    payload += tail;
    return std::vector<std::uint8_t>(payload.begin(), payload.end());
}

}  // namespace

TEST(decoderSohAtTheVeryEndOfTheBufferIsNotReadPast) {
    FixDecoder d;
    std::string s = "8=FIX.4.4";
    s.append(11, 'A');
    s.push_back('\x01');
    d.feed(bytes(s), 0, s.size());
    // Used to index buffer_[firstSoh + 1] past the live bytes.
    EXPECT_TRUE(!d.poll().has_value());
}

TEST(decoderRejectsAStreamThatIsNotFix) {
    FixDecoder d;
    std::vector<std::uint8_t> junk(64, 'A');
    d.feed(junk.data(), 0, junk.size());
    EXPECT_TRUE(throwsCorrupt([&] { d.poll(); }));
}

TEST(decoderCapsTheBeginStringScan) {
    FixDecoder d;
    std::string s = "8=";
    s.append(100, 'F');
    d.feed(bytes(s), 0, s.size());
    EXPECT_TRUE(throwsCorrupt([&] { d.poll(); }));
}

TEST(decoderCapsTheBodyLengthScan) {
    FixDecoder d;
    std::string s = std::string("8=FIX.4.4\x01") + "9=" + std::string(40, '1');
    d.feed(bytes(s), 0, s.size());
    EXPECT_TRUE(throwsCorrupt([&] { d.poll(); }));
}

TEST(decoderBoundsItsBufferWithoutPolling) {
    FixDecoder d;
    std::vector<std::uint8_t> chunk(1 << 20, 'A');
    EXPECT_TRUE(throwsCorrupt([&] {
        for (int i = 0; i < 8; ++i) {
            d.feed(chunk.data(), 0, chunk.size());
        }
    }));
}

TEST(decoderHonoursALoweredFrameLimit) {
    FixDecoder d;
    d.setMaxBodyLength(100);
    std::string s = std::string("8=FIX.4.4\x01") + "9=101\x01" + "35=0\x01";
    d.feed(bytes(s), 0, s.size());
    EXPECT_TRUE(throwsCorrupt([&] { d.poll(); }));
}

TEST(aBadChecksumIsGarbledAndLeavesTheStreamInSync) {
    FixDecoder d;
    auto bad = message(FixMessage::builder(FixMessage::HEARTBEAT));
    bad[bad.size() - 2] = bad[bad.size() - 2] == '0' ? '1' : '0';
    auto good = message(FixMessage::builder(FixMessage::HEARTBEAT));
    d.feed(bad.data(), 0, bad.size());
    d.feed(good.data(), 0, good.size());
    EXPECT_TRUE(throwsInvalid([&] { d.poll(); }));
    EXPECT_TRUE(d.poll().has_value());
}

TEST(parseRejectsMalformedAndDuplicatedHeaderTags) {
    EXPECT_TRUE(throwsInvalid([] { FixMessage::parse(frame(std::string("35=0\x01") + "abc=1\x01")); }));
    EXPECT_TRUE(throwsInvalid([] {
        FixMessage::parse(frame(std::string("35=0\x01") + "99999999999=1\x01"));
    }));
    EXPECT_TRUE(throwsInvalid([] {
        FixMessage::parse(frame(std::string("35=0\x01") + "34=1\x01" + "34=2\x01"));
    }));
}

TEST(getLongIsStrictAndChecksOverflow) {
    auto parsed = [](const std::string& v) {
        return FixMessage::parse(frame(std::string("35=0\x01") + "34=" + v + "\x01"));
    };
    EXPECT_TRUE(parsed("42").getLong(FixMessage::MSG_SEQ_NUM) == 42);
    EXPECT_TRUE(parsed("-7").getLong(FixMessage::MSG_SEQ_NUM) == -7);
    EXPECT_TRUE(parsed("100.00").getLong(FixMessage::MSG_SEQ_NUM) == 100);
    EXPECT_TRUE(throwsInvalid([&] { parsed("12ab").getLong(FixMessage::MSG_SEQ_NUM); }));
    EXPECT_TRUE(throwsInvalid([&] { parsed("1.5").getLong(FixMessage::MSG_SEQ_NUM); }));
    EXPECT_TRUE(throwsInvalid([&] { parsed("").getLong(FixMessage::MSG_SEQ_NUM); }));
    EXPECT_TRUE(throwsInvalid(
            [&] { parsed("99999999999999999999").getLong(FixMessage::MSG_SEQ_NUM); }));
}

TEST(getDoubleRefusesWhatFixFloatDoesNotAllow) {
    auto parsed = [](const std::string& v) {
        return FixMessage::parse(frame(std::string("35=0\x01") + "44=" + v + "\x01"));
    };
    EXPECT_NEAR(parsed("-1.25").getDouble(FixMessage::PRICE), -1.25, 0.0);
    for (const char* bad : {"nan", "inf", "1e5", "1.2.3", "-", ".", "12x"}) {
        EXPECT_TRUE(throwsInvalid([&] { parsed(bad).getDouble(FixMessage::PRICE); }));
    }
}

TEST(viewTruncatedAfterTheEqualsSignStaysInBounds) {
    std::string s = std::string("35=8\x01") + "150=";
    auto p = exact(s);
    FixExecReportView v;
    v.wrap(p.get(), 0, s.size());
    EXPECT_TRUE(v.execType() == 0);
}

TEST(viewTruncatedInsideATagStaysInBounds) {
    std::string s = std::string("35=8\x01") + "15";
    auto p = exact(s);
    FixExecReportView v;
    EXPECT_TRUE(!v.wrap(p.get(), 0, s.size()));
}

TEST(viewRefusesNonDigitTags) {
    std::string s = std::string("35=8\x01") + "1x5=F\x01";
    auto p = exact(s);
    FixExecReportView v;
    EXPECT_TRUE(!v.wrap(p.get(), 0, s.size()));
}

TEST(viewNumericFieldsFailLoudly) {
    FixExecReportView v;
    for (const std::string& bad :
         {std::string("35=8\x01") + "32=99999999999999999999999\x01",
          std::string("35=8\x01") + "14=12ab\x01",
          std::string("35=8\x01") + "31=1.0000000000000000000000001\x01",
          std::string("35=8\x01") + "31=abc\x01"}) {
        auto p = exact(bad);
        EXPECT_TRUE(throwsInvalid([&] { v.wrap(p.get(), 0, bad.size()); }));
    }
}

TEST(parseLongOrElseFallsBackOnOverflow) {
    std::string s = "99999999999999999999";
    EXPECT_TRUE(fp::parseLongOrElse(bytes(s), 0, s.size(), -1) == -1);
}

TEST(priceDecimalsNeverIndexesPastTheTable) {
    std::string ok = "0.000000000000000001";  // 18 decimals, the last table entry
    EXPECT_TRUE(fp::priceDecimals(bytes(ok), 0, ok.size()) == 18);
    std::string bad = ok + "0";
    EXPECT_TRUE(throwsInvalid([&] { fp::priceDecimals(bytes(bad), 0, bad.size()); }));
}

TEST(sbeCheckedWrapRefusesShortBuffersAndWrongTypes) {
    unsigned char buf[64] = {};
    OrderFlyweight o;
    o.wrap(buf, 0).encode(7, 1, Side::SELL, 100, 1.5, 9);
    OrderFlyweight reader;
    EXPECT_TRUE(reader.wrap(buf, 0, OrderFlyweight::BLOCK_LENGTH));
    EXPECT_TRUE(reader.orderId() == 7);
    EXPECT_TRUE(!reader.wrap(buf, 0, OrderFlyweight::BLOCK_LENGTH - 1));
    EXPECT_TRUE(!reader.wrap(buf, -1, 64));
    EXPECT_TRUE(!reader.wrap(nullptr, 0, 64));
    buf[OrderFlyweight::SIDE_OFFSET] = 9;  // not a Side
    EXPECT_TRUE(!reader.wrap(buf, 0, 64));
    TradeFlyweight t;
    QuoteFlyweight q;
    EXPECT_TRUE(!t.wrap(buf, 0, 64));  // an order is not a trade
    EXPECT_TRUE(!q.wrap(buf, 0, 64));
}

QTEST_MAIN()
