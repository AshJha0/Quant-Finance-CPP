// Port of HftOrderBookTest + HftOrderBookTifTest (Java): the venue-grade
// book's direct price-time semantics, rejection-code venue semantics,
// id-map churn under backward-shift deletion, and the time-in-force types
// (IOC / FOK / post-only) including the FOK atomicity property over 2000
// randomized books and the LONG-sentinel clamp pins.
#include "quantfinlib/orderbook/hft_order_book.hpp"

#include <cstdint>
#include <unordered_map>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "qtest.hpp"

using quantfinlib::HftOrderBook;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;

namespace {

constexpr int MIN_T = 900;
constexpr int MAX_T = 1100;

HftOrderBook book(int maxOrders) {
    return HftOrderBook(MIN_T, MAX_T, maxOrders);
}

// TIF tests use the Java TifTest band.
constexpr int TIF_MIN = 9000;
constexpr int TIF_MAX = 11000;

HftOrderBook tifBook() {
    return HftOrderBook(TIF_MIN, TIF_MAX, 1 << 10);
}

struct Trade {
    std::int64_t maker, taker, qty;
    int tick;
};

struct TradeLog {
    std::vector<Trade> trades;
    static void sink(void* ctx, std::int64_t m, std::int64_t t, int tick,
                     std::int64_t qty, std::int64_t) {
        static_cast<TradeLog*>(ctx)->trades.push_back({m, t, qty, tick});
    }
};

struct VolumeSink {
    std::int64_t traded = 0;
    static void sink(void* ctx, std::int64_t, std::int64_t, int, std::int64_t qty,
                     std::int64_t) {
        static_cast<VolumeSink*>(ctx)->traded += qty;
    }
};

std::int64_t totalBooked(HftOrderBook& b, int band) {
    std::vector<int> ticks(band);
    std::vector<std::int64_t> qtys(band);
    std::int64_t sum = 0;
    int n = b.snapshot(Side::BUY, ticks.data(), qtys.data(), band);
    for (int i = 0; i < n; i++) {
        sum += qtys[i];
    }
    n = b.snapshot(Side::SELL, ticks.data(), qtys.data(), band);
    for (int i = 0; i < n; i++) {
        sum += qtys[i];
    }
    return sum;
}

}  // namespace

// ------------------------------------------------------------------
// Direct semantics (HftOrderBookTest)
// ------------------------------------------------------------------

TEST(priceTimePriorityAndPartialFills) {
    HftOrderBook b = book(64);
    TradeLog log;
    b.tradeSink(&TradeLog::sink, &log);

    std::int64_t a1 = b.submitLimit(Side::SELL, 1001, 30, 1);  // first at the level
    std::int64_t a2 = b.submitLimit(Side::SELL, 1001, 30, 2);  // second: behind a1
    std::int64_t a3 = b.submitLimit(Side::SELL, 1002, 50, 3);  // worse price
    EXPECT_TRUE(b.bestAskTick() == 1001);
    EXPECT_TRUE(b.bestAskSize() == 60);

    // Crossing buy for 70: exhausts 1001 FIFO (a1 then a2), then 10 off a3.
    std::int64_t taker = b.submitLimit(Side::BUY, 1002, 70, 4);
    EXPECT_TRUE(log.trades.size() == 3);
    EXPECT_TRUE(log.trades[0].maker == a1);  // time priority within level
    EXPECT_TRUE(log.trades[0].qty == 30);
    EXPECT_TRUE(log.trades[1].maker == a2);
    EXPECT_TRUE(log.trades[2].maker == a3);  // price priority across levels
    EXPECT_TRUE(log.trades[2].tick == 1002);
    EXPECT_TRUE(log.trades[2].qty == 10);
    EXPECT_TRUE(log.trades[0].taker == taker);

    // a3 keeps its remainder; the taker rested nothing (fully filled).
    EXPECT_TRUE(b.openQuantity(a3) == 40);
    EXPECT_TRUE(b.openQuantity(taker) == 0);
    EXPECT_TRUE(b.bestAskTick() == 1002);
    EXPECT_TRUE(b.bestBidTick() == HftOrderBook::NO_BID);  // no bids
    EXPECT_TRUE(b.tradeCount() == 3);
}

TEST(restingCancelAndBestTracking) {
    HftOrderBook b = book(64);
    std::int64_t b1 = b.submitLimit(Side::BUY, 1000, 10, 1);
    std::int64_t b2 = b.submitLimit(Side::BUY, 995, 20, 2);
    // Levels > 64 ticks apart so best-advancement crosses bitmap words.
    std::int64_t b3 = b.submitLimit(Side::BUY, 920, 30, 3);
    EXPECT_TRUE(b.bestBidTick() == 1000);

    EXPECT_TRUE(b.cancel(b1));
    EXPECT_TRUE(b.bestBidTick() == 995);  // best walks down
    EXPECT_TRUE(b.cancel(b2));
    EXPECT_TRUE(b.bestBidTick() == 920);  // across the word boundary
    EXPECT_TRUE(!b.cancel(b1));           // idempotent: already gone
    EXPECT_TRUE(b.cancel(b3));
    EXPECT_TRUE(b.bestBidTick() == HftOrderBook::NO_BID);
    EXPECT_TRUE(b.restingOrders() == 0);

    // Depth snapshot into caller arrays: zero-alloc query contract.
    b.submitLimit(Side::SELL, 1001, 5, 4);
    b.submitLimit(Side::SELL, 1003, 7, 5);
    int ticks[4];
    std::int64_t qtys[4];
    EXPECT_TRUE(b.snapshot(Side::SELL, ticks, qtys, 4) == 2);
    EXPECT_TRUE(ticks[0] == 1001);
    EXPECT_TRUE(qtys[0] == 5);
    EXPECT_TRUE(ticks[1] == 1003);
}

TEST(rejectionCodesAreVenueSemantics) {
    HftOrderBook b = book(2);
    EXPECT_TRUE(b.submitLimit(Side::BUY, 1000, 0, 1) == HftOrderBook::REJECT_INVALID);
    EXPECT_TRUE(b.submitLimit(Side::BUY, MAX_T + 1, 10, 1) == HftOrderBook::REJECT_OUT_OF_BAND);
    EXPECT_TRUE(b.submitLimit(Side::BUY, MIN_T - 1, 10, 1) == HftOrderBook::REJECT_OUT_OF_BAND);

    EXPECT_TRUE(b.submitLimit(Side::SELL, 1001, 10, 1) > 0);
    EXPECT_TRUE(b.submitLimit(Side::SELL, 1002, 10, 2) > 0);
    // Pool full: a third RESTING order is rejected — and a capacity reject
    // can never follow executions, because a taker only has a remainder
    // after fully filling (and thereby freeing) every maker it crossed.
    VolumeSink vol;
    b.tradeSink(&VolumeSink::sink, &vol);
    EXPECT_TRUE(b.submitLimit(Side::BUY, 1000, 5, 3) == HftOrderBook::REJECT_POOL_FULL);
    EXPECT_TRUE(vol.traded == 0);
    // A crossing order through a full pool works fine: the fill frees the
    // maker's slot, and the remainder rests in it.
    std::int64_t id = b.submitLimit(Side::BUY, 1001, 25, 4);
    EXPECT_TRUE(id > 0);
    EXPECT_TRUE(vol.traded == 10);           // lifted the 1001 maker
    EXPECT_TRUE(b.openQuantity(id) == 15);   // remainder rested at 1001
    EXPECT_TRUE(b.bestAskTick() == 1002);

    EXPECT_THROWS(HftOrderBook(10, 5, 8));
    EXPECT_THROWS(HftOrderBook(0, 10, 0));
    EXPECT_TRUE(b.submitMarket(Side::BUY, 0, 1) == 0);
}

TEST(marketOrdersSweepAndNeverRest) {
    HftOrderBook b = book(64);
    b.submitLimit(Side::SELL, 1001, 10, 1);
    b.submitLimit(Side::SELL, 1005, 10, 2);
    EXPECT_TRUE(b.submitMarket(Side::BUY, 15, 3) == 15);  // sweeps two levels
    EXPECT_TRUE(b.qtyAtTick(Side::SELL, 1005) == 5);
    // Empty bid side: a sell market fills nothing and rests nothing.
    EXPECT_TRUE(b.submitMarket(Side::SELL, 20, 4) == 0);
    EXPECT_TRUE(b.restingOrders() == 1);  // only the 1005 remainder rests
}

// ------------------------------------------------------------------
// Id-map churn: backward-shift deletion under heavy add/cancel
// ------------------------------------------------------------------

TEST(idMapSurvivesHeavyChurn) {
    // Non-crossing orders only (bids far below asks) so every order rests:
    // pure map/pool churn, checked against a std::unordered_map reference.
    HftOrderBook b(0, 200, 1 << 12);
    std::unordered_map<std::int64_t, std::int64_t> expected;
    std::vector<std::int64_t> aliveIds;
    SplittableRandom rnd(7);
    for (int op = 0; op < 200000; op++) {
        if (rnd.nextBoolean() || aliveIds.empty()) {
            if (static_cast<int>(expected.size()) >= (1 << 12) - 1) {
                continue;  // stay under pool capacity
            }
            bool buy = rnd.nextBoolean();
            int tick = buy ? rnd.nextInt(90) : 110 + rnd.nextInt(90);
            std::int64_t qty = 1 + rnd.nextInt(1000);
            std::int64_t id = b.submitLimit(buy ? Side::BUY : Side::SELL, tick, qty, op);
            EXPECT_TRUE(id > 0);
            expected[id] = qty;
            aliveIds.push_back(id);
        } else {
            int pick = rnd.nextInt(static_cast<int>(aliveIds.size()));
            std::int64_t id = aliveIds[pick];
            aliveIds[pick] = aliveIds.back();
            aliveIds.pop_back();
            EXPECT_TRUE(b.cancel(id));
            expected.erase(id);
        }
    }
    // Every survivor must be found with its exact quantity; every cancelled
    // id must be gone — backward-shift left no ghosts.
    for (const auto& [id, qty] : expected) {
        EXPECT_TRUE(b.openQuantity(id) == qty);
    }
    EXPECT_TRUE(b.restingOrders() == static_cast<int>(expected.size()));
}

// ------------------------------------------------------------------
// IOC (HftOrderBookTifTest)
// ------------------------------------------------------------------

TEST(iocFillsWhatCrossesAndExpiresTheRest) {
    HftOrderBook b = tifBook();
    b.submitLimit(Side::SELL, 10000, 60, 1);
    b.submitLimit(Side::SELL, 10001, 50, 2);

    // Limit 10000: only the first level is within reach.
    EXPECT_TRUE(b.submitIoc(Side::BUY, 10000, 100, 3) == 60);
    // Nothing rested: the 40-share remainder expired.
    EXPECT_TRUE(b.qtyAtTick(Side::BUY, 10000) == 0);
    EXPECT_TRUE(b.restingOrders() == 1);
    EXPECT_TRUE(b.bestAskTick() == 10001);
}

TEST(iocAgainstAnEmptySideFillsNothing) {
    HftOrderBook b = tifBook();
    EXPECT_TRUE(b.submitIoc(Side::BUY, 10500, 100, 1) == 0);
    EXPECT_TRUE(b.submitIoc(Side::SELL, 9500, 100, 2) == 0);
    EXPECT_TRUE(b.restingOrders() == 0);
}

TEST(iocWithAggressiveOffBandLimitActsAsMarket) {
    HftOrderBook b = tifBook();
    b.submitLimit(Side::SELL, 10999, 30, 1);
    // Buy limit far above the band top: clamped, still lifts everything.
    EXPECT_TRUE(b.submitIoc(Side::BUY, TIF_MAX + 500, 30, 2) == 30);
    b.submitLimit(Side::BUY, 9001, 25, 3);
    EXPECT_TRUE(b.submitIoc(Side::SELL, TIF_MIN - 500, 25, 4) == 25);
}

// ------------------------------------------------------------------
// FOK
// ------------------------------------------------------------------

TEST(fokFillsFullyOrNotAtAll) {
    HftOrderBook b = tifBook();
    b.submitLimit(Side::SELL, 10000, 60, 1);
    b.submitLimit(Side::SELL, 10002, 50, 2);
    std::int64_t trades0 = b.tradeCount();

    // 100 within limit 10001: only 60 available -> killed, no trades.
    EXPECT_TRUE(b.submitFok(Side::BUY, 10001, 100, 3) == 0);
    EXPECT_TRUE(b.tradeCount() == trades0);
    EXPECT_TRUE(b.bestAskSize() == 60);

    // 100 within limit 10002: 110 available -> full fill across levels.
    EXPECT_TRUE(b.submitFok(Side::BUY, 10002, 100, 4) == 100);
    EXPECT_TRUE(b.qtyAtTick(Side::SELL, 10002) == 10);
    EXPECT_TRUE(b.qtyAtTick(Side::SELL, 10000) == 0);
}

TEST(fokProbeRespectsTheLimitPriceExactly) {
    HftOrderBook b = tifBook();
    b.submitLimit(Side::BUY, 10000, 40, 1);
    b.submitLimit(Side::BUY, 9998, 40, 2);
    // Sell 80 no lower than 9999: only the 40 at 10000 qualifies.
    EXPECT_TRUE(b.submitFok(Side::SELL, 9999, 80, 3) == 0);
    // Lowering the limit to 9998 brings the second level in.
    EXPECT_TRUE(b.submitFok(Side::SELL, 9998, 80, 4) == 80);
    EXPECT_TRUE(b.restingOrders() == 0);
}

// ------------------------------------------------------------------
// Post-only
// ------------------------------------------------------------------

TEST(postOnlyRestsWhenPassiveAndRejectsWhenItWouldCross) {
    HftOrderBook b = tifBook();
    b.submitLimit(Side::SELL, 10000, 50, 1);

    std::int64_t id = b.submitPostOnly(Side::BUY, 9999, 100, 2);
    EXPECT_TRUE(id > 0);  // passive post-only must rest
    EXPECT_TRUE(b.qtyAtTick(Side::BUY, 9999) == 100);

    // At or through the ask: rejected, book untouched, no trades.
    EXPECT_TRUE(b.submitPostOnly(Side::BUY, 10000, 100, 3)
                == HftOrderBook::REJECT_WOULD_CROSS);
    EXPECT_TRUE(b.submitPostOnly(Side::BUY, 10500, 100, 4)
                == HftOrderBook::REJECT_WOULD_CROSS);
    EXPECT_TRUE(b.tradeCount() == 0);
    EXPECT_TRUE(b.bestAskSize() == 50);

    // Mirror on the sell side.
    EXPECT_TRUE(b.submitPostOnly(Side::SELL, 9999, 10, 5)
                == HftOrderBook::REJECT_WOULD_CROSS);
    EXPECT_TRUE(b.submitPostOnly(Side::SELL, 10001, 10, 6) > 0);
}

TEST(sentinelLimitsMeanPureMarketAndNeverWrap) {
    // INT32_MIN/MAX are the documented pure-market sentinels: the
    // ladder-index math must not overflow them into the opposite meaning.
    HftOrderBook b = tifBook();
    b.submitLimit(Side::BUY, 10000, 40, 1);
    b.submitLimit(Side::SELL, 10002, 40, 2);

    // SELL at INT32_MIN = sell at any price: must sweep the bids.
    EXPECT_TRUE(b.submitIoc(Side::SELL, INT32_MIN, 40, 3) == 40);
    // BUY at INT32_MAX = buy at any price: must lift the asks.
    EXPECT_TRUE(b.submitIoc(Side::BUY, INT32_MAX, 40, 4) == 40);
    EXPECT_TRUE(b.restingOrders() == 0);

    // And the passive extremes stay passive: nothing can cross them.
    b.submitLimit(Side::BUY, 10000, 40, 5);
    b.submitLimit(Side::SELL, 10002, 40, 6);
    EXPECT_TRUE(b.submitIoc(Side::BUY, INT32_MIN, 40, 7) == 0);
    EXPECT_TRUE(b.submitIoc(Side::SELL, INT32_MAX, 40, 8) == 0);
    // FOK goes through the same probe: same sentinel semantics.
    EXPECT_TRUE(b.submitFok(Side::SELL, INT32_MIN, 40, 9) == 40);
    EXPECT_TRUE(b.submitFok(Side::SELL, INT32_MAX, 40, 10) == 0);
}

TEST(fokIsAtomicUnderRandomizedBooks) {
    // Property: a FOK either fills in full or leaves the book untouched —
    // the probe and the matching walk must agree forever.
    SplittableRandom rnd(99);
    for (int trial = 0; trial < 2000; trial++) {
        HftOrderBook b = tifBook();
        VolumeSink vol;
        b.tradeSink(&VolumeSink::sink, &vol);
        for (int i = 0; i < 30; i++) {
            b.submitLimit(rnd.nextBoolean() ? Side::BUY : Side::SELL,
                          TIF_MIN + rnd.nextInt(TIF_MAX - TIF_MIN + 1),
                          1 + rnd.nextInt(80), i);
        }
        std::int64_t tradedBefore = vol.traded;
        std::int64_t booked = totalBooked(b, TIF_MAX - TIF_MIN + 1);
        Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
        std::int64_t qty = 1 + rnd.nextInt(400);
        int limit = TIF_MIN + rnd.nextInt(TIF_MAX - TIF_MIN + 1);
        std::int64_t filled = b.submitFok(side, limit, qty, 999);
        if (filled == 0) {
            EXPECT_TRUE(vol.traded == tradedBefore);  // killed FOK emits no trades
            EXPECT_TRUE(totalBooked(b, TIF_MAX - TIF_MIN + 1) == booked);
        } else {
            EXPECT_TRUE(filled == qty);  // a FOK fill must be complete
            EXPECT_TRUE(totalBooked(b, TIF_MAX - TIF_MIN + 1) == booked - qty);
        }
    }
}

TEST(postOnlyValidatesLikeALimitOrder) {
    HftOrderBook b = tifBook();
    EXPECT_TRUE(b.submitPostOnly(Side::BUY, 10000, 0, 1) == HftOrderBook::REJECT_INVALID);
    EXPECT_TRUE(b.submitPostOnly(Side::BUY, TIF_MIN - 1, 10, 2)
                == HftOrderBook::REJECT_OUT_OF_BAND);
    // A resting post-only is a normal order: cancellable, fillable.
    std::int64_t id = b.submitPostOnly(Side::BUY, 10000, 25, 3);
    EXPECT_TRUE(b.submitIoc(Side::SELL, 10000, 25, 4) == 25);
    EXPECT_TRUE(b.openQuantity(id) == 0);
}

QTEST_MAIN()
