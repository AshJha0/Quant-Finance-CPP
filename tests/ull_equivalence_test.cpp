// HftOrderBook against the readable OrderBook, its executable specification.
// This is a randomized differential test, not a proof. Identical random
// operation streams (bit-exact java.util.SplittableRandom) drive both books,
// and after EVERY operation the test diffs:
//   - the trade tape: maker id, taker id, tick, quantity, timestamp, in order
//   - every return value (ids, fills, rejections, cancel results)
//   - both full depth ladders and every open-order quantity it touched
//
// OrderBook has no IOC, FOK or post-only, so the oracle composes them from
// the primitives it does have, in the most literal reading of each order type:
//   IOC       = limit, then cancel whatever rested
//   FOK       = sum the crossing depth first; all as an IOC, or nothing at all
//   post-only = refuse if it would cross, otherwise a plain limit
// A killed FOK or refused post-only consumes no id in either book, so ids
// stay equal and the tape diff needs no id mapping.
#include <cmath>
#include <cstdint>
#include <utility>
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
constexpr int ALL_LEVELS = MAX_T - MIN_T + 3;

double px(int tick) { return tick / 100.0; }
int tickOf(double price) { return static_cast<int>(std::llround(price * 100)); }

struct Trade {
    std::int64_t maker;
    std::int64_t taker;
    int tick;
    std::int64_t qty;
    std::int64_t ts;
    bool operator==(const Trade&) const = default;
};

void fastSink(void* ctx, std::int64_t maker, std::int64_t taker, int tick, std::int64_t qty,
              std::int64_t ts) {
    static_cast<std::vector<Trade>*>(ctx)->push_back({maker, taker, tick, qty, ts});
}

/** OrderBook plus the composed order types. */
class Oracle {
public:
    Oracle() : book("EQIV") {}

    std::int64_t ioc(Side side, int tick, std::int64_t qty, std::int64_t ts) {
        std::int64_t id = book.submitLimit(side, px(tick), qty, ts);
        const LimitOrder* rest = book.order(id);
        std::int64_t left = rest != nullptr ? rest->quantity : 0;
        if (rest != nullptr) {
            book.cancel(id);
            expiries++;
        }
        return qty - left;
    }

    std::int64_t fok(Side side, int tick, std::int64_t qty, std::int64_t ts) {
        std::int64_t avail = 0;
        for (const auto& [price, levelQty] :
             book.levels(side == Side::BUY ? Side::SELL : Side::BUY, ALL_LEVELS)) {
            bool crosses = side == Side::BUY ? tickOf(price) <= tick : tickOf(price) >= tick;
            if (crosses) {
                avail += levelQty;
            }
        }
        return avail >= qty ? ioc(side, tick, qty, ts) : 0;
    }

    std::int64_t postOnly(Side side, int tick, std::int64_t qty, std::int64_t ts) {
        double opposite = side == Side::BUY ? book.bestAsk() : book.bestBid();
        bool crosses = !std::isnan(opposite)
                && (side == Side::BUY ? tickOf(opposite) <= tick : tickOf(opposite) >= tick);
        return crosses ? HftOrderBook::REJECT_WOULD_CROSS
                       : book.submitLimit(side, px(tick), qty, ts);
    }

    OrderBook book;
    std::int64_t expiries = 0;   // oracle-only cancels, excluded from cancelCount
};

bool sameDepth(const HftOrderBook& fast, const OrderBook& reference, Side side) {
    static int ticks[ALL_LEVELS];
    static std::int64_t qtys[ALL_LEVELS];
    int n = fast.snapshot(side, ticks, qtys, ALL_LEVELS);
    auto levels = reference.levels(side, ALL_LEVELS);
    if (n != static_cast<int>(levels.size())) {
        return false;
    }
    for (int i = 0; i < n; i++) {
        if (ticks[i] != tickOf(levels[static_cast<std::size_t>(i)].first)
                || qtys[i] != levels[static_cast<std::size_t>(i)].second) {
            return false;
        }
    }
    return true;
}

struct Coverage {
    std::int64_t trades = 0, iocPartial = 0, fokFilled = 0, fokKilled = 0, postRested = 0,
                 postRefused = 0, cancelHit = 0, cancelMiss = 0;
};

Coverage runStream(std::uint64_t seed, int ops) {
    HftOrderBook fast(MIN_T, MAX_T, 1 << 14);
    Oracle oracle;
    std::vector<Trade> fastTape;
    std::vector<Trade> refTape;
    fast.tradeSink(&fastSink, &fastTape);
    oracle.book.addTradeListener([&](std::int64_t maker, std::int64_t taker, double price,
                                     std::int64_t qty, std::int64_t ts) {
        refTape.push_back({maker, taker, tickOf(price), qty, ts});
    });

    std::vector<std::int64_t> alive;   // ids, shared: both books issue the same ones
    SplittableRandom rnd(seed);
    Coverage cov;

    for (int op = 0; op < ops; op++) {
        Side side = rnd.nextBoolean() ? Side::BUY : Side::SELL;
        // A band around the middle keeps both sides populated and crossing;
        // the tails reach the ladder edges and the off-band IOC limits.
        int tick = 1000 + rnd.nextInt(41) - 20;
        std::int64_t qty = 1 + rnd.nextInt(100);
        int kind = rnd.nextInt(20);
        bool ok = true;

        if (kind < 8 || (kind < 12 && alive.empty())) {
            if (kind == 0) {
                tick = MIN_T + rnd.nextInt(MAX_T - MIN_T + 1);
            }
            std::int64_t f = fast.submitLimit(side, tick, qty, op);
            std::int64_t r = oracle.book.submitLimit(side, px(tick), qty, op);
            ok = f == r;
            const LimitOrder* rest = oracle.book.order(r);
            ok = ok && fast.openQuantity(f) == (rest != nullptr ? rest->quantity : 0);
            if (rest != nullptr) {
                alive.push_back(f);
            }
        } else if (kind < 12) {
            std::size_t pick = static_cast<std::size_t>(
                    rnd.nextInt(static_cast<int>(alive.size())));
            std::int64_t id = alive[pick];
            alive[pick] = alive.back();
            alive.pop_back();
            bool f = fast.cancel(id);
            ok = f == oracle.book.cancel(id);   // matching may have consumed it
            (f ? cov.cancelHit : cov.cancelMiss)++;
        } else if (kind < 14) {
            qty += rnd.nextInt(150);
            ok = fast.submitMarket(side, qty, op) == oracle.book.submitMarket(side, qty, op);
        } else if (kind < 16) {
            if (kind == 14 && rnd.nextInt(8) == 0) {
                tick = rnd.nextBoolean() ? MIN_T - 50 : MAX_T + 50;   // off-band limit
            }
            std::int64_t f = fast.submitIoc(side, tick, qty, op);
            ok = f == oracle.ioc(side, tick, qty, op);
            cov.iocPartial += f > 0 && f < qty;
        } else if (kind < 18) {
            qty *= 1 + rnd.nextInt(4);
            std::int64_t f = fast.submitFok(side, tick, qty, op);
            ok = f == oracle.fok(side, tick, qty, op);
            ok = ok && (f == 0 || f == qty);
            (f > 0 ? cov.fokFilled : cov.fokKilled)++;
        } else {
            std::int64_t f = fast.submitPostOnly(side, tick, qty, op);
            ok = f == oracle.postOnly(side, tick, qty, op);
            if (f > 0) {
                alive.push_back(f);
                cov.postRested++;
            } else {
                cov.postRefused++;
            }
        }

        EXPECT_TRUE(ok);
        EXPECT_TRUE(fastTape == refTape);
        EXPECT_TRUE(sameDepth(fast, oracle.book, Side::BUY));
        EXPECT_TRUE(sameDepth(fast, oracle.book, Side::SELL));
        if (qtest::failures() != 0) {
            std::printf("  first divergence: seed %llu op %d kind %d\n",
                        static_cast<unsigned long long>(seed), op, kind);
            break;
        }
        cov.trades += static_cast<std::int64_t>(fastTape.size());
        fastTape.clear();
        refTape.clear();
    }

    EXPECT_TRUE(fast.orderCount() == oracle.book.orderCount());
    EXPECT_TRUE(fast.cancelCount() == oracle.book.cancelCount() - oracle.expiries);
    EXPECT_TRUE(fast.tradeCount() == oracle.book.tradeCount());
    return cov;
}

}  // namespace

TEST(tapeAndDepthMatchTheOracleAfterEveryOperation) {
    Coverage total;
    for (std::uint64_t seed : {20260706ULL, 1ULL, 987654321ULL}) {
        Coverage c = runStream(seed, 100000);
        total.trades += c.trades;
        total.iocPartial += c.iocPartial;
        total.fokFilled += c.fokFilled;
        total.fokKilled += c.fokKilled;
        total.postRested += c.postRested;
        total.postRefused += c.postRefused;
        total.cancelHit += c.cancelHit;
        total.cancelMiss += c.cancelMiss;
    }
    // The run has to reach each branch, or the diff above proves nothing.
    EXPECT_TRUE(total.trades > 1000);
    EXPECT_TRUE(total.iocPartial > 100);
    EXPECT_TRUE(total.fokFilled > 100);
    EXPECT_TRUE(total.fokKilled > 100);
    EXPECT_TRUE(total.postRested > 100);
    EXPECT_TRUE(total.postRefused > 100);
    EXPECT_TRUE(total.cancelHit > 100);
    EXPECT_TRUE(total.cancelMiss > 100);
}

QTEST_MAIN()
