// Cache layout pins for the trading hot path, the kill race at submit, and
// FIX repeating groups and field sanitising.
#include <stdexcept>
#include <string>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/fix/execution_report.hpp"
#include "quantfinlib/fix/fix_message.hpp"
#include "quantfinlib/fix/fix_session.hpp"
#include "quantfinlib/trading/hft_order_gateway.hpp"

using namespace quantfinlib;

namespace {

std::size_t lineOf(std::size_t offset) { return offset / 64; }

template <typename F>
bool throwsInvalid(F&& f) {
    try {
        f();
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

FixMessage frame(const FixMessage::Builder& b) {
    return FixMessage::parse(b.encode("A", "B", 1, "20260101-00:00:00.000"));
}

// A group with entry tags 500 (delimiter) and 501.
FixMessage::Builder groupMessage(const std::string& count) {
    FixMessage::Builder b("X");
    b.field(FixMessage::SYMBOL, std::string("AAA"))
            .field(499, count)
            .field(500, std::string("a"))
            .field(501, std::string("1"))
            .field(500, std::string("a"))
            .field(500, std::string("c"))
            .field(501, std::string("3"))
            .field(FixMessage::TEXT, std::string("after"));
    return b;
}

bool tripAndAllow(void* gate, const HftOrderGateway::SelfTradeCandidate&) {
    static_cast<HftRiskGate*>(gate)->kill(true);
    return true;
}

}  // namespace

TEST(ringSlotIsOneAlignedCacheLine) {
    EXPECT_TRUE(OrderRingBuffer::slotBytes() == 64);
    OrderRingBuffer ring(16);
    EXPECT_TRUE(ring.slotMisalignment() == 0);
}

TEST(ringCarriesEveryFieldOfTheSlot) {
    OrderRingBuffer ring(4);
    EXPECT_TRUE(ring.publishMessage(2, 11, 7, 3, Side::SELL, 50, 1.25, 99));
    int seen = 0;
    ring.drainMessagesTo(
            [&](std::uint8_t type, std::int64_t id, std::int64_t ref, int sym, Side side,
                std::int64_t qty, double px, std::int64_t ts) {
                seen++;
                EXPECT_TRUE(type == 2 && id == 11 && ref == 7 && sym == 3);
                EXPECT_TRUE(side == Side::SELL && qty == 50 && px == 1.25 && ts == 99);
            },
            4);
    EXPECT_TRUE(seen == 1);
}

TEST(gateStateIsPaddedAgainstFalseSharing) {
    HftRiskGate gate(4);
    HftRiskGate::Layout l = gate.layout();
    EXPECT_TRUE(l.symbolStateAlign == 64);
    EXPECT_TRUE(l.symbolStateBytes == 128);
    EXPECT_TRUE(l.orderToMarketBytes == 64);   // market data never dirties the order line
    EXPECT_TRUE(l.rejections % 64 == 0);
    EXPECT_TRUE(lineOf(l.rejections) != lineOf(l.readOnlyHead));
    EXPECT_TRUE(lineOf(l.kill) > lineOf(l.rejections));
    EXPECT_TRUE(lineOf(l.limits) > lineOf(l.kill));
}

TEST(paddedGateKeepsSymbolsApart) {
    HftRiskGate gate(3);
    gate.onFill(1, Side::BUY, 5);
    gate.setReferencePrice(2, 10.0);
    gate.halt(0, true);
    EXPECT_TRUE(gate.position(0) == 0 && gate.position(1) == 5 && gate.position(2) == 0);
    EXPECT_TRUE(gate.referencePrice(2) == 10.0);
    EXPECT_TRUE(std::isnan(gate.referencePrice(1)));
}

TEST(orderThatBeatsTheKillIsNotMistakenForReduceOnly) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(1000);
    gate.disableReferenceCheck();
    HftOrderGateway gw(64, gate, false);
    int delivered = 0;
    gw.addOrderListener([&](std::int64_t, int, Side, std::int64_t, double, std::int64_t) {
        delivered++;
    });
    EXPECT_TRUE(gw.submit(0, Side::SELL, 1, 100.0, 1) > 0);
    gw.pollOnce();
    EXPECT_TRUE(delivered == 1);

    // The hook runs inside submit, after the gate passed the order, which is
    // exactly the window: the kill lands, then the slot is filled in.
    gw.setSelfTradeHook(&tripAndAllow, &gate);
    EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 101.0, 2) > 0);
    EXPECT_TRUE(gate.isKilled());
    gw.pollOnce();
    EXPECT_TRUE(delivered == 1);
    EXPECT_TRUE(gw.blockedByKillCount() == 1);
}

TEST(sweepCoversEveryBlockOfTheTable) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(100000);
    gate.disableReferenceCheck();
    HftOrderGateway gw(1024, gate, false, 512);
    for (int i = 0; i < 300; i++) {
        EXPECT_TRUE(gw.submit(0, Side::BUY, 1, 100.0, 1) > 0);
    }
    EXPECT_TRUE(gw.cancelAll(2) == 300);
    EXPECT_TRUE(gw.openOrderCount() == 0);
}

TEST(listenersAddedLaterAllRun) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(100).maxOrderNotional(1e9).maxPositionQuantity(1000);
    gate.disableReferenceCheck();
    HftOrderGateway gw(64, gate, false);
    int hits = 0;
    for (int i = 0; i < 5; i++) {
        gw.addVenueListener([&](const VenueMessage&) { hits++; });
    }
    gw.submit(0, Side::BUY, 1, 100.0, 1);
    gw.pollOnce();
    EXPECT_TRUE(hits == 5);
}

TEST(repeatedTagsSurviveInWireOrder) {
    FixMessage m = frame(groupMessage("3"));
    EXPECT_TRUE(m.count(500) == 3);
    EXPECT_TRUE((m.getAll(500) == std::vector<std::string>{"a", "a", "c"}));
    EXPECT_TRUE(m.getString(500) == "c");   // map access unchanged: last wins
    EXPECT_TRUE(m.fields().front().tag == FixMessage::BEGIN_STRING_TAG);
    EXPECT_TRUE(m.fields().back().tag == FixMessage::CHECK_SUM);
}

TEST(groupSplitsEntriesAndStopsAtTheFirstOutsider) {
    FixMessage m = frame(groupMessage("3"));
    auto g = m.group(499, {500, 501});
    EXPECT_TRUE(g.size() == 3);
    EXPECT_TRUE(g[0].size() == 2 && g[0][1].value == "1");
    EXPECT_TRUE(g[1].size() == 1 && g[1][0].value == "a");
    EXPECT_TRUE(g[2].size() == 2 && g[2][1].value == "3");
    EXPECT_TRUE(m.group(777, {500}).empty());
}

TEST(groupCountMismatchIsRefused) {
    EXPECT_TRUE(throwsInvalid([] { frame(groupMessage("2")).group(499, {500, 501}); }));
    EXPECT_TRUE(throwsInvalid([] { frame(groupMessage("x")).group(499, {500, 501}); }));
    EXPECT_TRUE(throwsInvalid([] { frame(groupMessage("-3")).group(499, {500, 501}); }));
    // 500 is not the delimiter here: an entry with no opening delimiter.
    EXPECT_TRUE(throwsInvalid([] { frame(groupMessage("3")).group(499, {501, 500}); }));
}

TEST(builderRefusesFieldInjection) {
    std::string forged = std::string("X") + FixMessage::SOH + "38=1000000";
    EXPECT_TRUE(throwsInvalid([&] { FixMessage::Builder("D").field(FixMessage::CL_ORD_ID, forged); }));
    EXPECT_TRUE(throwsInvalid([&] {
        FixSession::Config(std::string("A") + FixMessage::SOH + "56=X", "B", 30);
    }));
    FixMessage::Builder ok("D");
    ok.field(FixMessage::TEXT, std::string("a=b"));   // '=' is legal in a value
    EXPECT_TRUE(frame(ok).getString(FixMessage::TEXT) == "a=b");
}

TEST(executionReportRefusesAmbiguity) {
    ExecutionReport e{"V", "E", 'F', '2', "1", "AAA", Side::BUY, 1, 1, 1, 0, 1};
    FixMessage::Builder dup = e.toBuilder();
    dup.field(FixMessage::CUM_QTY, std::string("5"));
    EXPECT_TRUE(throwsInvalid([&] { ExecutionReport::fromMessage(frame(dup)); }));
    FixMessage::Builder badSide("8");
    badSide.field(FixMessage::ORDER_ID, std::string("V"))
            .field(FixMessage::EXEC_ID, std::string("E"))
            .field(FixMessage::EXEC_TYPE, 'F')
            .field(FixMessage::ORD_STATUS, '2')
            .field(FixMessage::SYMBOL, std::string("AAA"))
            .field(FixMessage::SIDE, '7');
    EXPECT_TRUE(throwsInvalid([&] { ExecutionReport::fromMessage(frame(badSide)); }));
}

QTEST_MAIN()
