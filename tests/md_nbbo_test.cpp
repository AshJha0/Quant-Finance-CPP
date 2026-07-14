// Port of NbboTest (Java): venue aggregation with inside-size summation,
// venue-down removal, change-only listener semantics, locked/crossed
// flags, high-priced-symbol mid-tick overflow safety, empty-side
// sentinels, and the randomized differential test that checks the
// fast-path-guarded Nbbo against brute-force recomputation over 20,000
// updates using the bit-exact SplittableRandom port (same generator the
// Java test uses, so this is the same stream shape). The Java
// allocation-free-loop test has no C++ analogue and is not ported.
#include "quantfinlib/marketdata/nbbo.hpp"

#include <cmath>
#include <vector>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "qtest.hpp"

using quantfinlib::Nbbo;
using quantfinlib::SplittableRandom;

TEST(aggregatesBestAcrossVenuesAndSumsInsideSize) {
    Nbbo n(3);
    n.onVenueQuote(0, 17500, 100, 17502, 200, 1);
    n.onVenueQuote(1, 17501, 300, 17503, 50, 2);   // better bid
    n.onVenueQuote(2, 17501, 200, 17502, 80, 3);   // ties both sides

    EXPECT_TRUE(n.bidTick() == 17501);
    EXPECT_TRUE(n.bidSize() == 500);                // venues 1 + 2
    EXPECT_TRUE(n.bidVenues() == 0b110ULL);
    EXPECT_TRUE(n.askTick() == 17502);
    EXPECT_TRUE(n.askSize() == 280);                // venues 0 + 2
    EXPECT_TRUE(n.askVenues() == 0b101ULL);
    EXPECT_NEAR(n.midTick(), 17501.5, 1e-9);
}

TEST(venueDownRemovesItsQuotesFromTheInside) {
    Nbbo n(2);
    n.onVenueQuote(0, 100, 10, 102, 10, 1);
    n.onVenueQuote(1, 101, 20, 103, 20, 2);
    EXPECT_TRUE(n.bidTick() == 101);
    EXPECT_TRUE(n.onVenueDown(1, 3));
    EXPECT_TRUE(n.bidTick() == 100);
    EXPECT_TRUE(n.askTick() == 102);
    EXPECT_TRUE(n.onVenueDown(0, 4));
    EXPECT_TRUE(n.bidTick() == Nbbo::NO_BID);
    EXPECT_TRUE(n.askTick() == Nbbo::NO_ASK);
}

namespace {
struct FireCounter {
    long fired = 0;
    static void onNbbo(void* ctx, int, std::int64_t, int, std::int64_t, std::int64_t) {
        static_cast<FireCounter*>(ctx)->fired++;
    }
};
}  // namespace

TEST(listenerFiresOnlyOnInsideChanges) {
    Nbbo n(2);
    FireCounter fc;
    n.listener(&FireCounter::onNbbo, &fc);
    n.onVenueQuote(0, 100, 10, 102, 10, 1);
    EXPECT_TRUE(fc.fired == 1);
    // Venue 1 quoting outside the inside: no NBBO change, no callback.
    EXPECT_TRUE(!n.onVenueQuote(1, 99, 5, 103, 5, 2));
    EXPECT_TRUE(fc.fired == 1);
    // Size change AT the inside is a change.
    EXPECT_TRUE(n.onVenueQuote(0, 100, 15, 102, 10, 3));
    EXPECT_TRUE(fc.fired == 2);
    EXPECT_TRUE(n.updateCount() == 3);
    EXPECT_TRUE(n.changeCount() == 2);
}

TEST(lockedAndCrossedMarketsAreFlagged) {
    Nbbo n(2);
    n.onVenueQuote(0, 100, 10, 100, 10, 1);            // same venue locks
    EXPECT_TRUE(n.locked());
    EXPECT_TRUE(!n.crossed());
    n.onVenueQuote(1, 101, 10, Nbbo::NO_ASK, 0, 2);    // bid through the offer
    EXPECT_TRUE(n.crossed());
}

TEST(midTickSurvivesHighPricedSymbols) {
    // Two ticks near 2^30 (a ~$110k symbol in 0.0001 ticks) must not wrap
    // the 32-bit sum negative.
    Nbbo n(1);
    n.onVenueQuote(0, 1100000000, 10, 1100000100, 10, 1);
    EXPECT_NEAR(n.midTick(), 1100000050.0, 1e-9);
}

TEST(offInsideFastPathNeverMissesAnInsideChange) {
    // Randomized differential check: the fast-path-guarded Nbbo must
    // agree with brute-force recomputation after every update.
    Nbbo n(8);
    int bids[8][2];                     // {tick, size}
    int asks[8][2];
    for (int k = 0; k < 8; k++) {
        bids[k][0] = Nbbo::NO_BID;      // untouched venue = absent,
        asks[k][0] = Nbbo::NO_ASK;      // not "quoting at tick 0"
        bids[k][1] = 0;
        asks[k][1] = 0;
    }
    SplittableRandom rnd(11);
    for (int i = 0; i < 20000; i++) {
        int v = rnd.nextInt(8);
        int bid = 990 + rnd.nextInt(20);
        int ask = bid + 1 + rnd.nextInt(5);
        int bidSz = rnd.nextInt(4) == 0 ? 0 : 1 + rnd.nextInt(500);
        int askSz = rnd.nextInt(4) == 0 ? 0 : 1 + rnd.nextInt(500);
        n.onVenueQuote(v, bid, bidSz, ask, askSz, i);
        bids[v][0] = bidSz > 0 ? bid : Nbbo::NO_BID;
        bids[v][1] = bidSz;
        asks[v][0] = askSz > 0 ? ask : Nbbo::NO_ASK;
        asks[v][1] = askSz;

        int bb = Nbbo::NO_BID;
        std::int64_t bbSz = 0;
        int bo = Nbbo::NO_ASK;
        std::int64_t boSz = 0;
        for (int k = 0; k < 8; k++) {
            if (bids[k][0] != Nbbo::NO_BID) {
                if (bids[k][0] > bb) {
                    bb = bids[k][0];
                    bbSz = bids[k][1];
                } else if (bids[k][0] == bb) {
                    bbSz += bids[k][1];
                }
            }
            if (asks[k][0] != Nbbo::NO_ASK) {
                if (asks[k][0] < bo) {
                    bo = asks[k][0];
                    boSz = asks[k][1];
                } else if (asks[k][0] == bo) {
                    boSz += asks[k][1];
                }
            }
        }
        EXPECT_TRUE(bb == n.bidTick());
        EXPECT_TRUE(bbSz == n.bidSize());
        EXPECT_TRUE(bo == n.askTick());
        EXPECT_TRUE(boSz == n.askSize());
    }
}

TEST(emptySidesUseSentinels) {
    Nbbo n(1);
    n.onVenueQuote(0, 100, 10, Nbbo::NO_ASK, 0, 1);
    EXPECT_TRUE(n.bidTick() == 100);
    EXPECT_TRUE(n.askTick() == Nbbo::NO_ASK);
    EXPECT_TRUE(std::isnan(n.midTick()));
    // Zero-size sides are treated as absent regardless of price.
    n.onVenueQuote(0, 100, 0, 102, 0, 2);
    EXPECT_TRUE(n.bidTick() == Nbbo::NO_BID);
}

QTEST_MAIN()
