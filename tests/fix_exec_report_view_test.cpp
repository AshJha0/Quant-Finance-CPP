// Port of FixExecReportViewTest.java: the garbage-free ExecutionReport view,
// proven against the FixMessage codec -- messages built through the
// validated Builder must read back field-identical through the view.
#include "quantfinlib/fix/fix_exec_report_view.hpp"

#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/fix_order_encoder.hpp"
#include "quantfinlib/orderbook/side.hpp"

using quantfinlib::FixExecReportView;
using quantfinlib::FixMessage;

namespace {

/** A venue fill built through the existing validated string codec. */
std::vector<std::uint8_t> fill(const std::string& clOrdId, const std::string& symbol, char side,
                               std::int64_t lastQty, const std::string& lastPx,
                               std::int64_t cumQty, std::int64_t leavesQty) {
    return FixMessage::builder("8")
            .field(FixMessage::ORDER_ID, std::string("V-1"))
            .field(FixMessage::EXEC_ID, std::string("E-1"))
            .field(FixMessage::EXEC_TYPE, std::string("F"))
            .field(FixMessage::ORD_STATUS, std::string("2"))
            .field(FixMessage::CL_ORD_ID, clOrdId)
            .field(FixMessage::SYMBOL, symbol)
            .field(FixMessage::SIDE, std::string(1, side))
            .field(FixMessage::LAST_QTY, std::to_string(lastQty))
            .field(FixMessage::LAST_PX, lastPx)
            .field(FixMessage::CUM_QTY, std::to_string(cumQty))
            .field(FixMessage::LEAVES_QTY, std::to_string(leavesQty))
            .encode("VENUE", "QFL", 7, "20260706-12:00:00.000");
}

}  // namespace

TEST(counterpartyFormattedClOrdIdMapsToNotOursInsteadOfThrowing) {
    // ClOrdID is free-format FIX: unsolicited venue reports (expires,
    // restatements, drop-copy) carry THEIR formats. A foreign id must never
    // kill the message pump -- it reads as the -1 "not ours" sentinel and
    // the rest of the report still parses.
    const char* foreignIds[] = {"ORD-2024-17", "abc", "NONE", "550e8400-e29b-41d4"};
    for (const char* foreign : foreignIds) {
        std::vector<std::uint8_t> bytes = fill(foreign, "EURUSD", '1', 100, "1.08505", 100, 0);
        FixExecReportView view;
        EXPECT_TRUE(view.wrap(bytes.data(), 0, bytes.size()));
        EXPECT_TRUE(view.clOrdId() == -1);
        EXPECT_TRUE(view.lastQty() == 100);
        EXPECT_TRUE(view.lastPxMantissa() == 108505);
    }
}

TEST(negativePricesRoundTripThroughTheView) {
    // Forward points / negative rates: '-' must parse, not garble.
    std::vector<std::uint8_t> bytes = fill("42", "EURUSD", '1', 100, "-0.00035", 100, 0);
    FixExecReportView view;
    EXPECT_TRUE(view.wrap(bytes.data(), 0, bytes.size()));
    EXPECT_TRUE(view.lastPxMantissa() == -35);
    EXPECT_TRUE(view.lastPxDecimals() == 5);
}

TEST(readsAFillFieldIdenticalToTheStringCodec) {
    std::vector<std::uint8_t> bytes = fill("987654321", "EURUSD", '1', 250'000, "1.08505", 750'000, 250'000);
    FixExecReportView view;
    EXPECT_TRUE(view.wrap(bytes.data(), 0, bytes.size()));

    EXPECT_TRUE(view.clOrdId() == 987654321L);
    EXPECT_TRUE(view.execType() == 'F');
    EXPECT_TRUE(view.ordStatus() == '2');
    EXPECT_TRUE(view.side() == '1');
    EXPECT_TRUE(view.lastQty() == 250'000);
    EXPECT_TRUE(view.cumQty() == 750'000);
    EXPECT_TRUE(view.leavesQty() == 250'000);
    // Price as scaled long -- the same representation FixOrderEncoder takes
    // in: the FIX round trip never touches a double.
    EXPECT_TRUE(view.lastPxMantissa() == 108505);
    EXPECT_TRUE(view.lastPxDecimals() == 5);
    // Symbol compared in place, never materialized.
    std::string eurusd = "EURUSD";
    std::string usdjpy = "USDJPY";
    EXPECT_TRUE(view.symbolEquals(reinterpret_cast<const std::uint8_t*>(eurusd.data()), eurusd.size()));
    EXPECT_TRUE(!view.symbolEquals(reinterpret_cast<const std::uint8_t*>(usdjpy.data()), usdjpy.size()));
}

TEST(nonExecReportsAreRejectedEarly) {
    // A heartbeat (35=0) and an encoder-built NewOrderSingle (35=D): wrap
    // must return false for both and never mislead the fill handler.
    std::vector<std::uint8_t> heartbeat =
            FixMessage::builder("0").encode("VENUE", "QFL", 8, "20260706-12:00:00.000");
    FixExecReportView view;
    EXPECT_TRUE(!view.wrap(heartbeat.data(), 0, heartbeat.size()));

    quantfinlib::FixOrderEncoder enc("QFL", "VENUE", 4, 512);
    enc.registerSymbol(0, "EURUSD");
    enc.encodeLimit(1, 1, 0, quantfinlib::Side::BUY, 1, 108505, 5, 1'800'000'000'000LL);
    EXPECT_TRUE(!view.wrap(enc.buffer(), static_cast<std::size_t>(enc.offset()),
                           static_cast<std::size_t>(enc.length())));
}

TEST(priceAndQuantityEdgeCases) {
    FixExecReportView view;
    // Integer price, zero-fraction quantity ("100.0" style feeds).
    std::vector<std::uint8_t> a = fill("1", "USDJPY", '2', 100, "155", 100, 0);
    EXPECT_TRUE(view.wrap(a.data(), 0, a.size()));
    EXPECT_TRUE(view.lastPxMantissa() == 155);
    EXPECT_TRUE(view.lastPxDecimals() == 0);

    std::vector<std::uint8_t> b = FixMessage::builder("8")
                                          .field(FixMessage::CL_ORD_ID, std::string("2"))
                                          .field(FixMessage::LAST_QTY, std::string("100.00"))
                                          .encode("VENUE", "QFL", 9, "20260706-12:00:00.000");
    EXPECT_TRUE(view.wrap(b.data(), 0, b.size()));
    EXPECT_TRUE(view.lastQty() == 100);

    // A REAL fractional quantity must fail loudly, never truncate.
    std::vector<std::uint8_t> c = FixMessage::builder("8")
                                          .field(FixMessage::CL_ORD_ID, std::string("3"))
                                          .field(FixMessage::LAST_QTY, std::string("100.5"))
                                          .encode("VENUE", "QFL", 10, "20260706-12:00:00.000");
    EXPECT_THROWS(view.wrap(c.data(), 0, c.size()));
}

QTEST_MAIN()
