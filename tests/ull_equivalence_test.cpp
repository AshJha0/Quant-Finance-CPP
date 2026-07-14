// Port of HftOrderBookTest.matchesTheReferenceOrderBookUnderRandomOperations
// (Java), widened to 100,000 operations: the readable OrderBook is the
// executable specification of HftOrderBook. Identical random operation
// streams (bit-exact java.util.SplittableRandom) must produce identical
// best prices, sizes, per-level depth and traded volume at every check.
#include <cmath>
#include <cstdint>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/orderbook/order_book.hpp"
#include "qtest.hpp"

using quantfinlib::HftOrderBook;
using quantfinlib::LimitOrder;
using quantfinlib::OrderBook;
using quantfinlib::Side;
using quantfinlib::SplittableRandom;

namespace {

constexpr int MIN_T = 900;
constexpr int MAX_T = 1100;

struct VolumeSink {
    std::int64_t traded = 0;
    static void sink(void* ctx, std::int64_t, std::int64_t, int, std::int64_t qty,
                     std::int64_t) {
        static_cast<VolumeSink*>(ctx)->traded += qty;
    }
};

bool compareBooks(HftOrderBook& fast, OrderBook& reference) {
    bool ok = true;
    double refBid = reference.bestBid();
    if (std::isnan(refBid)) {
        ok = ok && fast.bestBidTick() == HftOrderBook::NO_BID;
    } else {
        ok = ok && std::llround(refBid * 100) == fast.bestBidTick();
        ok = ok && reference.bestBidSize() == fast.bestBidSize();
    }
    double refAsk = reference.bestAsk();
    if (std::isnan(refAsk)) {
        ok = ok && fast.bestAskTick() == HftOrderBook::NO_ASK;
    } else {
        ok = ok && std::llround(refAsk * 100) == fast.bestAskTick();
        ok = ok && reference.bestAskSize() == fast.bestAskSize();
    }
    // Top-of-book depth, level by level, both sides.
    for (Side side : {Side::BUY, Side::SELL}) {
        for (const auto& [price, qty] : reference.levels(side, 10)) {
            ok = ok && qty == fast.qtyAtTick(side, static_cast<int>(std::llround(price * 100)));
        }
    }
    return ok;
}

}  // namespace

TEST(matchesTheReferenceOrderBookUnder100kRandomOperations) {
    HftOrderBook fast(MIN_T, MAX_T, 1 << 14);
    OrderBook reference("EQIV");
    VolumeSink fastVol;
    std::int64_t refTraded = 0;
    fast.tradeSink(&VolumeSink::sink, &fastVol);
    reference.addTradeListener(
            [&refTraded](std::int64_t, std::int64_t, double, std::int64_t qty,
                         std::int64_t) { refTraded += qty; });

    // Alive resting pairs (fastId, refId) for cancel targeting.
    std::vector<std::pair<std::int64_t, std::int64_t>> alive;
    SplittableRandom rnd(20260706);

    const int OPS = 100000;
    for (int op = 0; op < OPS; op++) {
        int kind = rnd.nextInt(10);
        if (kind < 6 || alive.empty()) {
            Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
            int tick = MIN_T + rnd.nextInt(MAX_T - MIN_T + 1);
            std::int64_t qty = 1 + rnd.nextInt(100);
            std::int64_t fastId = fast.submitLimit(side, tick, qty, op);
            std::int64_t refId = reference.submitLimit(side, tick / 100.0, qty, op);
            EXPECT_TRUE(fastId > 0);
            // Both books must agree on whether the order rested.
            std::int64_t fastOpen = fast.openQuantity(fastId);
            const LimitOrder* refOrder = reference.order(refId);
            EXPECT_TRUE((refOrder != nullptr) == (fastOpen > 0));
            if (fastOpen > 0) {
                EXPECT_TRUE(refOrder->quantity == fastOpen);
                alive.emplace_back(fastId, refId);
            }
        } else if (kind < 9) {
            int pick = rnd.nextInt(static_cast<int>(alive.size()));
            auto pair = alive[pick];
            alive.erase(alive.begin() + pick);
            // A pair can have been consumed by matching since we saved it.
            EXPECT_TRUE(reference.cancel(pair.second) == fast.cancel(pair.first));
        } else {
            Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
            std::int64_t qty = 1 + rnd.nextInt(150);
            fast.submitMarket(side, qty, op);
            reference.submitMarket(side, qty, op);
        }

        if (op % 500 == 0 || op == OPS - 1) {
            EXPECT_TRUE(compareBooks(fast, reference));
            EXPECT_TRUE(refTraded == fastVol.traded);
        }
    }
    EXPECT_TRUE(fastVol.traded > 0);  // the run must actually exercise matching
    // Surveillance counters marched in lockstep too.
    EXPECT_TRUE(fast.orderCount() == reference.orderCount());
    EXPECT_TRUE(fast.cancelCount() == reference.cancelCount());
    EXPECT_TRUE(fast.tradeCount() == reference.tradeCount());
}

QTEST_MAIN()
