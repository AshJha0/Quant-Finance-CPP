// Regression tests for the book's input limits (2026-11-06 review): the
// per-order quantity limit that keeps level totals inside int64, and tick
// arithmetic that must not wrap on extreme prices or ladders. Run under
// UBSan: the wraps were signed overflow before they were wrong answers.
#include <cstdint>
#include <limits>
#include <stdexcept>

#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "qtest.hpp"

using quantfinlib::HftOrderBook;
using quantfinlib::Side;

namespace {

constexpr std::int64_t I64_MAX = std::numeric_limits<std::int64_t>::max();
constexpr int I32_MAX = std::numeric_limits<int>::max();
constexpr int I32_MIN = std::numeric_limits<int>::min();

bool ctorThrows(int lo, int hi, int orders) {
    try {
        HftOrderBook b(lo, hi, orders);
    } catch (const std::invalid_argument&) {
        return true;
    }
    return false;
}

}  // namespace

TEST(levelTotalCannotOverflowAtTheLimit) {
    constexpr int ORDERS = 8;
    HftOrderBook b(100, 200, ORDERS);
    std::int64_t lim = b.maxOrderQuantity();
    EXPECT_TRUE(lim == I64_MAX / ORDERS);
    for (int i = 0; i < ORDERS; i++) {
        EXPECT_TRUE(b.submitLimit(Side::BUY, 150, lim, i) > 0);
    }
    EXPECT_TRUE(b.bestBidSize() == lim * ORDERS);
    EXPECT_TRUE(b.bestBidSize() > 0);
    // The FOK probe sums the same quantities.
    EXPECT_TRUE(b.submitFok(Side::SELL, 150, lim, 9) == lim);
    EXPECT_TRUE(b.bestBidSize() == lim * (ORDERS - 1));
}

TEST(everyEntryPointRefusesAQuantityAboveTheLimit) {
    HftOrderBook b(100, 200, 4);
    std::int64_t over = b.maxOrderQuantity() + 1;
    EXPECT_TRUE(b.submitLimit(Side::BUY, 150, 10, 0) > 0);
    std::int64_t orders = b.orderCount();
    EXPECT_TRUE(b.submitLimit(Side::BUY, 150, over, 0) == HftOrderBook::REJECT_QUANTITY_LIMIT);
    EXPECT_TRUE(b.submitPostOnly(Side::BUY, 140, over, 0)
                == HftOrderBook::REJECT_QUANTITY_LIMIT);
    EXPECT_TRUE(b.submitMarket(Side::SELL, over, 0) == 0);
    EXPECT_TRUE(b.submitIoc(Side::SELL, 100, over, 0) == 0);
    EXPECT_TRUE(b.submitFok(Side::SELL, 100, over, 0) == 0);
    EXPECT_TRUE(b.submitLimit(Side::BUY, 150, I64_MAX, 0)
                == HftOrderBook::REJECT_QUANTITY_LIMIT);
    EXPECT_TRUE(b.orderCount() == orders);   // no id, no counter, no trade
    EXPECT_TRUE(b.tradeCount() == 0);
    EXPECT_TRUE(b.bestBidSize() == 10);
}

TEST(extremeTicksAreOutOfBandNotWrapped) {
    // priceTick - minTick wrapped here: INT_MAX - (-5) lands inside the band.
    HftOrderBook b(-5, 5, 4);
    EXPECT_TRUE(b.submitLimit(Side::BUY, I32_MAX, 1, 0) == HftOrderBook::REJECT_OUT_OF_BAND);
    EXPECT_TRUE(b.submitPostOnly(Side::SELL, I32_MAX, 1, 0)
                == HftOrderBook::REJECT_OUT_OF_BAND);
    EXPECT_TRUE(b.qtyAtTick(Side::BUY, I32_MAX) == 0);
    HftOrderBook c(5, 15, 4);
    EXPECT_TRUE(c.submitLimit(Side::SELL, I32_MIN, 1, 0) == HftOrderBook::REJECT_OUT_OF_BAND);
    EXPECT_TRUE(c.submitPostOnly(Side::BUY, I32_MIN, 1, 0)
                == HftOrderBook::REJECT_OUT_OF_BAND);
    EXPECT_TRUE(c.qtyAtTick(Side::SELL, I32_MIN) == 0);
    EXPECT_TRUE(b.restingOrders() == 0 && c.restingOrders() == 0);
}

TEST(constructorRejectsSpansAndPoolsThatWouldWrap) {
    EXPECT_TRUE(ctorThrows(I32_MIN, I32_MAX, 4));     // span wraps to 0 in int
    EXPECT_TRUE(ctorThrows(-2, I32_MAX, 4));          // span wraps negative in int
    EXPECT_TRUE(ctorThrows(0, I32_MAX, 4));           // a best ask could equal NO_ASK
    EXPECT_TRUE(ctorThrows(I32_MIN, 0, 4));           // a best bid could equal NO_BID
    EXPECT_TRUE(ctorThrows(10, 5, 4));
    EXPECT_TRUE(ctorThrows(0, 10, 0));
    EXPECT_TRUE(ctorThrows(0, 10, -1));
    EXPECT_TRUE(ctorThrows(0, 10, HftOrderBook::MAX_ORDERS + 1));   // map size would wrap
}

TEST(aBookAtTheTopOfTheIntRangeStillWorks) {
    HftOrderBook b(I32_MAX - 10, I32_MAX - 1, 4);
    EXPECT_TRUE(b.submitLimit(Side::SELL, I32_MAX - 1, 3, 0) > 0);
    EXPECT_TRUE(b.bestAskTick() == I32_MAX - 1);
    EXPECT_TRUE(b.submitIoc(Side::BUY, I32_MAX, 3, 0) == 3);
    EXPECT_TRUE(b.restingOrders() == 0);
}

QTEST_MAIN()
