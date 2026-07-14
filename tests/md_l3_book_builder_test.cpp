// Port of L3BookBuilderTest (Java): full-depth book semantics (adds,
// executes, cancels, deletes, replace priority loss, best-price
// advancement across emptied levels), unknown-ref/off-band counting,
// exact own-order queue-position tracking (the pins that justify the
// O(1) maintenance rule: executions consume the head, cancels are ahead
// only by insertion order), wire dispatch parity with direct calls,
// per-locate message filtering, duplicate-ref rejection, idempotent
// tracking, and replace-to-off-band drop semantics. The Java
// allocation-free-loop tests have no C++ analogue and are not ported.
#include "quantfinlib/marketdata/l3_book_builder.hpp"

#include <climits>

#include "quantfinlib/marketdata/itch_codec.hpp"
#include "qtest.hpp"

using quantfinlib::ItchCodec;
using quantfinlib::L3BookBuilder;
using quantfinlib::Side;

namespace {
constexpr int LOCATE = 42;
constexpr int MIN = 1700000;    // $170.0000 in 0.0001 ticks
constexpr int MAX = 1800000;

L3BookBuilder book() {
    return L3BookBuilder(LOCATE, MIN, MAX, 1 << 12);
}
}  // namespace

TEST(attributedAddsBookExactlyLikePlainAdds) {
    // On real TotalView a large share of adds are type 'F' (add with MPID
    // attribution). Dropping that dispatch case would silently ignore
    // them and corrupt the whole book -- this pins the path, which needs
    // a hand-crafted frame (no encoder produces F).
    EXPECT_TRUE(ItchCodec::length(ItchCodec::ADD_MPID) == 40);
    L3BookBuilder b = book();
    unsigned char buf[64] = {};
    ItchCodec::encodeAdd(buf, 0, LOCATE, 1L, 7, ItchCodec::BUY, 100, ItchCodec::packStock("TEST"),
                         1750000);
    buf[0] = ItchCodec::ADD_MPID;                   // same layout + 4 MPID bytes
    buf[36] = 'M';
    buf[37] = 'P';
    buf[38] = 'I';
    buf[39] = 'D';
    EXPECT_TRUE(b.onMessage(buf, 0) == 40);          // consumed as a 40-byte F frame
    EXPECT_TRUE(b.openQuantity(7) == 100);           // the attributed add is IN the book
    EXPECT_TRUE(b.bestBidTick() == 1750000);
}

// ------------------------------------------------------------------
// Book semantics
// ------------------------------------------------------------------

TEST(addsBuildDepthAndBestPrices) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    b.onAdd(2, Side::BUY, 200, 1750000);
    b.onAdd(3, Side::BUY, 50, 1749900);
    b.onAdd(4, Side::SELL, 80, 1750100);
    b.onAdd(5, Side::SELL, 20, 1750300);

    EXPECT_TRUE(b.bestBidTick() == 1750000);
    EXPECT_TRUE(b.bestBidSize() == 300);
    EXPECT_TRUE(b.bestAskTick() == 1750100);
    EXPECT_TRUE(b.bestAskSize() == 80);
    EXPECT_TRUE(b.qtyAtTick(Side::BUY, 1749900) == 50);

    int ticks[8];
    std::int64_t qtys[8];
    EXPECT_TRUE(b.snapshot(Side::BUY, ticks, qtys, 8) == 2);
    EXPECT_TRUE(ticks[0] == 1750000);
    EXPECT_TRUE(qtys[0] == 300);
    EXPECT_TRUE(ticks[1] == 1749900);
    EXPECT_TRUE(b.snapshot(Side::SELL, ticks, qtys, 8) == 2);
    EXPECT_TRUE(ticks[0] == 1750100);
}

TEST(executionsConsumeAndRemove) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::SELL, 100, 1750100);
    b.onExecute(1, 40);
    EXPECT_TRUE(b.bestAskSize() == 60);
    EXPECT_TRUE(b.openQuantity(1) == 60);
    b.onExecute(1, 60);
    EXPECT_TRUE(b.openQuantity(1) == 0);
    EXPECT_TRUE(b.bestAskTick() == INT32_MAX);
    EXPECT_TRUE(b.restingOrders() == 0);
}

TEST(cancelReducesInPlaceAndDeleteRemoves) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    b.onAdd(2, Side::BUY, 50, 1750000);
    b.onCancel(1, 30);
    EXPECT_TRUE(b.openQuantity(1) == 70);
    EXPECT_TRUE(b.bestBidSize() == 120);
    b.onDelete(1);
    EXPECT_TRUE(b.openQuantity(1) == 0);
    EXPECT_TRUE(b.bestBidSize() == 50);
    // Over-cancel clamps, never goes negative.
    b.onCancel(2, 500);
    EXPECT_TRUE(b.restingOrders() == 0);
    EXPECT_TRUE(b.bestBidTick() == INT32_MIN);
}

TEST(replaceMovesPriceAndLosesPriority) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::SELL, 100, 1750100);
    b.onAdd(2, Side::SELL, 60, 1750200);
    b.onReplace(1, 11, 100, 1750200);
    EXPECT_TRUE(b.openQuantity(1) == 0);
    EXPECT_TRUE(b.openQuantity(11) == 100);
    EXPECT_TRUE(b.bestAskTick() == 1750200);
    EXPECT_TRUE(b.bestAskSize() == 160);
    // Priority lost: an execution at the level consumes ref 2 first.
    b.onExecute(2, 60);
    EXPECT_TRUE(b.bestAskSize() == 100);
    EXPECT_TRUE(b.openQuantity(11) == 100);
}

TEST(bestPricesAdvanceAcrossEmptiedLevels) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 10, 1750000);
    b.onAdd(2, Side::BUY, 20, 1748000);
    b.onAdd(3, Side::BUY, 30, 1745000);
    b.onDelete(1);
    EXPECT_TRUE(b.bestBidTick() == 1748000);
    b.onDelete(2);
    EXPECT_TRUE(b.bestBidTick() == 1745000);
}

TEST(unknownRefsAndOffBandAddsAreCountedNotThrown) {
    L3BookBuilder b = book();
    b.onExecute(999, 10);
    b.onDelete(998);
    b.onCancel(997, 5);
    b.onReplace(996, 995, 10, 1750000);
    EXPECT_TRUE(b.unknownRefCount() == 4);
    EXPECT_TRUE(!b.onAdd(1, Side::BUY, 100, MIN - 1));
    EXPECT_TRUE(!b.onAdd(2, Side::BUY, 100, MAX + 1));
    EXPECT_TRUE(b.outOfBandCount() == 2);
    EXPECT_TRUE(b.restingOrders() == 0);
}

// ------------------------------------------------------------------
// Queue-position tracking
// ------------------------------------------------------------------

TEST(queuePositionTracksExecutionsCancelsAndDeletesAhead) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);    // ahead
    b.onAdd(2, Side::BUY, 50, 1750000);     // ahead
    b.onAdd(3, Side::BUY, 75, 1750000);     // ours
    b.onAdd(4, Side::BUY, 40, 1750000);     // behind
    EXPECT_TRUE(b.track(3));
    EXPECT_TRUE(b.sharesAhead(3) == 150);

    b.onCancel(1, 30);                        // ahead shrinks in place
    EXPECT_TRUE(b.sharesAhead(3) == 120);
    b.onExecute(1, 70);                       // head fully executes
    EXPECT_TRUE(b.sharesAhead(3) == 50);
    b.onDelete(4);                            // behind us: no effect
    EXPECT_TRUE(b.sharesAhead(3) == 50);
    b.onExecute(2, 50);                       // remaining ahead executes
    EXPECT_TRUE(b.sharesAhead(3) == 0);
    b.onExecute(3, 75);                       // we fill: tracking ends
    EXPECT_TRUE(b.sharesAhead(3) == -1);
}

TEST(ordersJoiningBehindNeverChangeSharesAhead) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::SELL, 100, 1750100);
    b.onAdd(2, Side::SELL, 60, 1750100);
    EXPECT_TRUE(b.track(2));
    EXPECT_TRUE(b.sharesAhead(2) == 100);
    b.onAdd(3, Side::SELL, 500, 1750100);     // joins behind
    EXPECT_TRUE(b.sharesAhead(2) == 100);
    b.onDelete(3);                             // behind: still no effect
    EXPECT_TRUE(b.sharesAhead(2) == 100);
    // Same-price activity on the OTHER side must not interfere either.
    b.onAdd(4, Side::BUY, 20, 1749000);
    b.onDelete(4);
    EXPECT_TRUE(b.sharesAhead(2) == 100);
}

TEST(replaceOfAnAheadOrderCreditsItsFullRemainder) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    b.onAdd(2, Side::BUY, 50, 1750000);
    EXPECT_TRUE(b.track(2));
    EXPECT_TRUE(b.sharesAhead(2) == 100);
    // Ahead order replaces away to a new price: it leaves our queue.
    b.onReplace(1, 11, 100, 1749000);
    EXPECT_TRUE(b.sharesAhead(2) == 0);
    // The replacement itself, being a new order elsewhere, is irrelevant.
    EXPECT_TRUE(b.openQuantity(11) == 100);
}

TEST(trackingOwnReplacedOrderEnds) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    EXPECT_TRUE(b.track(1));
    b.onReplace(1, 2, 100, 1750000);
    EXPECT_TRUE(b.sharesAhead(1) == -1);        // old ref gone
    EXPECT_TRUE(b.track(2));                    // re-track the new ref
    EXPECT_TRUE(b.sharesAhead(2) == 0);
}

TEST(trackMidQueueInitializesFromAWalk) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::SELL, 10, 1750100);
    b.onAdd(2, Side::SELL, 20, 1750100);
    b.onAdd(3, Side::SELL, 30, 1750100);
    b.onAdd(4, Side::SELL, 40, 1750100);
    EXPECT_TRUE(b.track(3));                    // tracked long after joining
    EXPECT_TRUE(b.sharesAhead(3) == 30);
    EXPECT_TRUE(!b.track(999));                 // unknown ref
}

// ------------------------------------------------------------------
// Wire dispatch
// ------------------------------------------------------------------

TEST(wireMessagesProduceTheSameBookAsDirectCalls) {
    L3BookBuilder wire = book();
    L3BookBuilder direct = book();
    std::int64_t stock = ItchCodec::packStock("TEST");
    unsigned char buf[64] = {};

    ItchCodec::encodeAdd(buf, 0, LOCATE, 1L, 1, ItchCodec::BUY, 100, stock, 1750000);
    EXPECT_TRUE(wire.onMessage(buf, 0) == 36);
    direct.onAdd(1, Side::BUY, 100, 1750000);

    ItchCodec::encodeAdd(buf, 0, LOCATE, 2L, 2, ItchCodec::SELL, 80, stock, 1750100);
    wire.onMessage(buf, 0);
    direct.onAdd(2, Side::SELL, 80, 1750100);

    ItchCodec::encodeExecuted(buf, 0, LOCATE, 3L, 2, 30, 1L);
    wire.onMessage(buf, 0);
    direct.onExecute(2, 30);

    ItchCodec::encodeReplace(buf, 0, LOCATE, 4L, 1, 11, 60, 1749900);
    wire.onMessage(buf, 0);
    direct.onReplace(1, 11, 60, 1749900);

    ItchCodec::encodeCancel(buf, 0, LOCATE, 5L, 11, 10);
    wire.onMessage(buf, 0);
    direct.onCancel(11, 10);

    ItchCodec::encodeTrade(buf, 0, LOCATE, 6L, 0, ItchCodec::BUY, 25, stock, 1750050, 2L);
    wire.onMessage(buf, 0);
    direct.onTrade(1750050);

    EXPECT_TRUE(direct.bestBidTick() == wire.bestBidTick());
    EXPECT_TRUE(direct.bestBidSize() == wire.bestBidSize());
    EXPECT_TRUE(direct.bestAskTick() == wire.bestAskTick());
    EXPECT_TRUE(direct.bestAskSize() == wire.bestAskSize());
    EXPECT_TRUE(direct.restingOrders() == wire.restingOrders());
    EXPECT_TRUE(direct.lastTradeTick() == wire.lastTradeTick());
    EXPECT_TRUE(wire.lastTradeTick() == 1750050);
}

TEST(messagesForOtherLocatesAreIgnored) {
    L3BookBuilder b = book();
    unsigned char buf[64] = {};
    ItchCodec::encodeAdd(buf, 0, LOCATE + 1, 1L, 1, ItchCodec::BUY, 100,
                         ItchCodec::packStock("OTHR"), 1750000);
    EXPECT_TRUE(b.onMessage(buf, 0) == 0);
    EXPECT_TRUE(b.restingOrders() == 0);
}

TEST(duplicateAddsAreRejectedNotCorrupting) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    EXPECT_TRUE(!b.onAdd(1, Side::BUY, 999, 1751000));  // replayed ref must be dropped
    EXPECT_TRUE(b.duplicateRefCount() == 1);
    EXPECT_TRUE(b.openQuantity(1) == 100);
    EXPECT_TRUE(b.restingOrders() == 1);
    // The single delete fully removes it: no phantom second node.
    b.onDelete(1);
    EXPECT_TRUE(b.restingOrders() == 0);
    EXPECT_TRUE(b.openQuantity(1) == 0);
}

TEST(trackingIsIdempotent) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::BUY, 100, 1750000);
    b.onAdd(2, Side::BUY, 50, 1750000);
    EXPECT_TRUE(b.track(2));
    EXPECT_TRUE(b.track(2));                  // a retried track must be a no-op
    b.onExecute(1, 100);
    EXPECT_TRUE(b.sharesAhead(2) == 0);
    b.onExecute(2, 50);                       // we fill: tracking ends fully
    EXPECT_TRUE(b.sharesAhead(2) == -1);
    // No leaked rows: all 64 tracking slots must be usable afterwards.
    for (int i = 0; i < 64; i++) {
        b.onAdd(100 + i, Side::SELL, 10, 1750100);
        EXPECT_TRUE(b.track(100 + i));
    }
}

TEST(replaceToOffBandPriceDropsTheOrderAndCountsIt) {
    L3BookBuilder b = book();
    b.onAdd(1, Side::SELL, 100, 1750100);
    std::int64_t before = b.outOfBandCount();
    b.onReplace(1, 2, 100, MAX + 1000);       // re-price outside the band
    EXPECT_TRUE(b.openQuantity(1) == 0);      // the original must be gone
    EXPECT_TRUE(b.openQuantity(2) == 0);      // the off-band replacement is invisible
    EXPECT_TRUE(b.outOfBandCount() == before + 1);
    EXPECT_TRUE(b.replaceCount() == 1);
    EXPECT_TRUE(b.restingOrders() == 0);
}

QTEST_MAIN()
