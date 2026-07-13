// Port of the Bar record's behavior. The Java project pins Bar
// construction inside MarketDataTest (new Bar(1, 100, 102, 99, 101, 5000));
// the derived-value pins below are hand-computed from that same bar.
#include "quantfinlib/data/bar.hpp"

#include "qtest.hpp"

using quantfinlib::Bar;

TEST(barAccessorsAndDerivedValues) {
    // The MarketDataTest bar: ts=1, O=100, H=102, L=99, C=101, V=5000.
    Bar b(1, 100, 102, 99, 101, 5000);
    EXPECT_TRUE(b.timestamp() == 1);
    EXPECT_NEAR(b.open(), 100.0, 0.0);
    EXPECT_NEAR(b.high(), 102.0, 0.0);
    EXPECT_NEAR(b.low(), 99.0, 0.0);
    EXPECT_NEAR(b.close(), 101.0, 0.0);
    EXPECT_NEAR(b.volume(), 5000.0, 0.0);
    // typical = (102 + 99 + 101) / 3 = 302/3 = 100.66666666666667
    EXPECT_NEAR(b.typicalPrice(), 302.0 / 3.0, 1e-13);
    // range = 102 - 99 = 3
    EXPECT_NEAR(b.range(), 3.0, 0.0);
    // close 101 > open 100 -> bullish
    EXPECT_TRUE(b.isBullish());
}

TEST(barValidationHighBelowLowThrows) {
    // high 99 < low 100 violates the only record invariant.
    EXPECT_THROWS(Bar(1, 100, 99, 100, 100, 0));
    // high == low is a legal (flat) bar, range 0.
    Bar flat(2, 50, 50, 50, 50, 10);
    EXPECT_NEAR(flat.range(), 0.0, 0.0);
}

TEST(barBearishAndDojiAreNotBullish) {
    // close 99 < open 100 -> bearish
    Bar bear(3, 100, 101, 98, 99, 100);
    EXPECT_TRUE(!bear.isBullish());
    // close == open (doji): strict > means not bullish, matching Java
    Bar doji(4, 100, 101, 99, 100, 100);
    EXPECT_TRUE(!doji.isBullish());
}

QTEST_MAIN()
