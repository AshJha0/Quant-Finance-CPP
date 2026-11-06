// Queue fill simulator, the paper gateway on top of it (H16), and the
// backtest seam.
#include <cmath>
#include <map>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/queue_execution.hpp"
#include "quantfinlib/execution/queue_fill_simulator.hpp"
#include "quantfinlib/trading/paper_trading_gateway.hpp"

using namespace quantfinlib;
using Sim = QueueFillSimulator;
using Report = QueueFillSimulator::Report;

namespace {

Sim::Config zero() { return Sim::Config::zeroLatency(); }

std::int64_t filled(const std::vector<Report>& reports) {
    std::int64_t q = 0;
    for (const Report& r : reports) {
        q += r.type == Sim::ReportType::FILL ? r.quantity : 0;
    }
    return q;
}

}  // namespace

// ------------------------------------------------------------ queue

TEST(passiveOrderWaitsBehindDisplayedSizeAndFillsPartially) {
    Sim s(zero());
    s.onBook(1, 100, 500, 101, 500);
    std::int64_t id = s.submitLimit(2, Side::BUY, 200, 100);
    EXPECT_NEAR(s.sharesAhead(id), 500, 1e-9);
    s.onTrade(3, 100, 400);                     // all of it ahead of us
    EXPECT_TRUE(filled(s.drainReports(3)) == 0);
    EXPECT_NEAR(s.sharesAhead(id), 100, 1e-9);
    s.onTrade(4, 100, 150);                     // 100 ahead, 50 reach us
    auto r = s.drainReports(4);
    EXPECT_TRUE(filled(r) == 50);
    EXPECT_NEAR(r[0].price, 100, 1e-12);
    EXPECT_TRUE(r[0].passive);
    s.onTrade(5, 100, 1000);                    // the rest, and no more
    EXPECT_TRUE(filled(s.drainReports(5)) == 150);
}

TEST(cancelsAheadAdvanceTheQueueProRataAndGrowthBehindDoesNot) {
    Sim s(zero());
    s.onBook(1, 100, 1000, 101, 500);
    std::int64_t id = s.submitLimit(2, Side::BUY, 100, 100);
    s.onBook(3, 100, 1500, 101, 500);           // joins behind us
    EXPECT_NEAR(s.sharesAhead(id), 1000, 1e-9);
    s.onBook(4, 100, 750, 101, 500);            // half the others left
    EXPECT_NEAR(s.sharesAhead(id), 500, 1e-9);
}

TEST(orderInsideTheSpreadHasNothingAheadAndOneBehindTheTouchHasNoQueueYet) {
    Sim s(zero());
    s.onBook(1, 100, 500, 102, 500);
    std::int64_t inside = s.submitLimit(2, Side::BUY, 10, 101);
    EXPECT_NEAR(s.sharesAhead(inside), 0, 1e-12);
    std::int64_t behind = s.submitLimit(2, Side::BUY, 10, 99);
    EXPECT_TRUE(std::isnan(s.sharesAhead(behind)));
    s.onBook(3, 99, 300, 102, 500);             // touch reaches it: back of the level
    EXPECT_NEAR(s.sharesAhead(behind), 300, 1e-9);
}

TEST(ownEarlierOrderIsAheadOfOwnLaterOrder) {
    Sim s(zero());
    s.onBook(1, 100, 100, 101, 500);
    s.submitLimit(2, Side::BUY, 50, 100);
    std::int64_t second = s.submitLimit(2, Side::BUY, 50, 100);
    EXPECT_NEAR(s.sharesAhead(second), 150, 1e-9);
    s.onTrade(3, 100, 170);                     // 50 to the first, 20 to the second
    EXPECT_TRUE(filled(s.drainReports(3)) == 70);
}

TEST(aTradeCannotFillMoreOwnQuantityThanItsSize) {
    Sim s(zero());
    s.onBook(1, 100, 0, 103, 500);
    s.submitLimit(2, Side::BUY, 80, 102);
    s.submitLimit(2, Side::BUY, 80, 101);
    s.onTrade(3, 100, 100, Side::SELL);         // through both limits
    EXPECT_TRUE(filled(s.drainReports(3)) == 100);
}

TEST(aPrintAtTheMidFillsNothingUnlessTheAggressorIsStated) {
    Sim s(zero());
    s.onBook(1, 100, 0, 102, 0);
    s.submitLimit(2, Side::BUY, 10, 101);
    s.onTrade(3, 101, 50);
    EXPECT_TRUE(filled(s.drainReports(3)) == 0);
    s.onTrade(4, 101, 50, Side::SELL);
    EXPECT_TRUE(filled(s.drainReports(4)) == 10);
}

TEST(crossedRestingOrderFillsAtItsLimitAndOnlyUpToDisplayedSize) {
    Sim s(zero());
    s.onBook(1, 100, 500, 102, 500);
    s.submitLimit(2, Side::BUY, 300, 100);
    s.onBook(3, 98, 500, 99, 120);
    auto r = s.drainReports(3);
    EXPECT_TRUE(filled(r) == 120);
    EXPECT_NEAR(r[0].price, 100, 1e-12);
}

TEST(crossedPartFillLeavesTheRemainderQueuedForItsTrueSize) {
    Sim s(zero());
    s.onBook(1, 100, 0, 102, 500);
    std::int64_t id = s.submitLimit(2, Side::BUY, 300, 100);
    s.onBook(3, 100, 40, 100, 120);             // locked: 120 hit us, 180 left
    EXPECT_TRUE(filled(s.drainReports(3)) == 120);
    EXPECT_NEAR(s.sharesAhead(id), 0, 1e-9);
    s.onBook(4, 100, 40, 101, 120);
    s.onTrade(5, 100, 1000);
    EXPECT_TRUE(filled(s.drainReports(5)) == 180);
}

// ------------------------------------------------------------ aggression

TEST(largeMarketOrderFillsOnlyDisplayedDepth) {
    Sim s(zero());
    s.onBook(1, 100, 500, 101, 300);
    s.submitMarket(2, Side::BUY, 1'000'000);
    auto r = s.drainReports(2);
    EXPECT_TRUE(filled(r) == 300);
    EXPECT_TRUE(r.back().type == Sim::ReportType::CANCELLED);
    EXPECT_TRUE(r.back().quantity == 999'700);
}

TEST(depthTakenIsNotAvailableAgainUntilItRecoversOrThePriceMoves) {
    Sim::Config c = zero();
    c.depthRecoveryNanos = 100;
    Sim s(c);
    s.onBook(1, 100, 500, 101, 300);
    s.submitMarket(2, Side::BUY, 200);
    s.submitMarket(3, Side::BUY, 200);          // only 100 left
    EXPECT_TRUE(filled(s.drainReports(3)) == 300);
    s.onBook(4, 100, 500, 101, 300);            // the tape cannot show our take
    s.submitMarket(5, Side::BUY, 200);
    EXPECT_TRUE(filled(s.drainReports(5)) == 0);
    s.onBook(200, 100, 500, 101, 300);
    s.submitMarket(201, Side::BUY, 200);
    EXPECT_TRUE(filled(s.drainReports(201)) == 200);
}

TEST(marketableLimitRemainderRestsAtItsLimit) {
    Sim s(zero());
    s.onBook(1, 100, 500, 101, 300);
    std::int64_t id = s.submitLimit(2, Side::BUY, 500, 101);
    EXPECT_TRUE(filled(s.drainReports(2)) == 300);
    EXPECT_NEAR(s.sharesAhead(id), 0, 1e-12);
}

// ------------------------------------------------------------ latency

TEST(orderSeesTheMarketAtArrivalNotAtSend) {
    Sim::Config c = zero();
    c.orderLatencyNanos = 100;
    Sim s(c);
    s.onBook(1, 100, 500, 101, 500);
    s.submitMarket(10, Side::BUY, 50);
    s.onBook(50, 104, 500, 105, 500);           // moves while in flight
    s.advanceTo(110);
    EXPECT_TRUE(s.drainReports(110).empty());   // done at 110, reported at 210
    auto r = s.drainReports(210);
    EXPECT_NEAR(r[0].price, 105, 1e-12);
    EXPECT_TRUE(r[0].exchangeNanos == 110);
}

TEST(aCancelCanLoseTheRaceToAFill) {
    Sim::Config c = zero();
    c.orderLatencyNanos = 100;
    Sim s(c);
    s.onBook(1, 100, 0, 101, 500);
    std::int64_t id = s.submitLimit(2, Side::BUY, 50, 100);
    s.advanceTo(102);
    EXPECT_TRUE(s.cancel(110, id));
    s.onTrade(150, 100, 80);                    // before the cancel lands at 210
    s.advanceTo(400);
    EXPECT_TRUE(filled(s.drainReports(400)) == 50);
}

TEST(strategySeesTheBookOneFeedLatencyLate) {
    Sim::Config c = zero();
    c.marketDataLatencyNanos = 100;
    Sim s(c);
    s.onBook(1000, 100, 500, 101, 500);
    s.onBook(1050, 110, 500, 111, 500);
    EXPECT_TRUE(!s.visibleBook(1050 - 100).has_value());
    EXPECT_NEAR(s.visibleBook(1100)->bid, 100, 1e-12);
    EXPECT_NEAR(s.visibleBook(1150)->bid, 110, 1e-12);
}

TEST(sameSeedRepeatsExactlyAndAnotherSeedDiffers) {
    auto run = [](std::uint64_t seed) {
        Sim::Config c = zero();
        c.orderLatencyNanos = 100;
        c.orderLatencyJitterNanos = 1000;
        c.seed = seed;
        Sim s(c);
        s.onBook(1, 100, 1'000'000, 101, 1'000'000);
        std::vector<std::int64_t> times;
        for (int i = 0; i < 50; i++) {
            s.submitMarket(10 + i * 2000, Side::BUY, 10);
            s.onBook(12 + i * 2000 + 1500, 100, 1'000'000, 101, 1'000'000);
        }
        s.advanceTo(1'000'000);
        for (const Report& r : s.drainReports(1'000'000)) {
            times.push_back(r.exchangeNanos * 1000 + r.quantity);
        }
        return times;
    };
    EXPECT_TRUE(run(7) == run(7));
    EXPECT_TRUE(!run(7).empty());
    EXPECT_TRUE(run(7) != run(8));
}

// ------------------------------------------------------------ mark-out

TEST(passiveFillRunOverShowsAdverseSelection) {
    Sim::Config c = zero();
    c.markoutHorizonNanos = 100;
    Sim s(c);
    s.onBook(1, 100, 0, 101, 500);
    s.submitLimit(2, Side::BUY, 10, 100);
    s.onTrade(3, 100, 10);
    EXPECT_TRUE(s.adverseSelection().unmarkedQuantity == 10);
    EXPECT_TRUE(s.adverseSelection().passive.quantity == 0);   // no peek before the horizon
    s.onBook(50, 95, 100, 96, 100);
    EXPECT_TRUE(s.adverseSelection().passive.quantity == 0);
    s.onBook(103, 90, 100, 92, 100);
    const auto& a = s.adverseSelection();
    EXPECT_TRUE(a.unmarkedQuantity == 0);
    EXPECT_NEAR(a.passive.markout, (91 - 100) * 10.0, 1e-9);
    EXPECT_NEAR(a.passive.adverseMove, (91 - 100.5) * 10.0, 1e-9);
    EXPECT_NEAR(a.passive.markoutBps(), -900, 1e-9);
    EXPECT_TRUE(std::isnan(a.aggressive.markoutBps()));
}

TEST(aggressiveSellMarkoutHasTheRightSign) {
    Sim::Config c = zero();
    c.markoutHorizonNanos = 10;
    Sim s(c);
    s.onBook(1, 100, 500, 102, 500);
    s.submitMarket(2, Side::SELL, 10);
    s.onBook(20, 96, 100, 98, 100);
    EXPECT_NEAR(s.adverseSelection().aggressive.markout, (100 - 97) * 10.0, 1e-9);
}

TEST(simulatorRefusesBadInput) {
    Sim s(zero());
    EXPECT_THROWS(s.onBook(1, std::nan(""), 1, 101, 1));
    EXPECT_THROWS(s.onBook(1, 102, 1, 101, 1));
    EXPECT_THROWS(s.onBook(1, 100, -1, 101, 1));
    s.onBook(5, 100, 1, 101, 1);
    EXPECT_THROWS(s.onBook(4, 100, 1, 101, 1));
    EXPECT_THROWS(s.submitMarket(6, Side::BUY, 0));
    EXPECT_THROWS(s.submitLimit(6, Side::BUY, 1, -1));
    EXPECT_THROWS(s.onTrade(6, 100, -5, Side::SELL));
    Sim::Config c;
    c.orderLatencyNanos = -1;
    EXPECT_THROWS(Sim{c});
}

// ------------------------------------------------------------ paper gateway (H16)

TEST(paperLargeOrderDoesNotFillEntirelyAtTheTouch) {
    auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1e9, 0, Sim::Config::zeroLatency());
    gw.onQuote("X", 1, 99, 100, 100, 100);
    std::int64_t id = gw.submitMarket("X", Side::BUY, 1'000'000);
    EXPECT_TRUE(gw.filledQuantity(id) == 100);
    EXPECT_TRUE(gw.status(id) == OrderStatus::CANCELED);
    EXPECT_NEAR(gw.position("X"), 100, 1e-9);
}

TEST(paperGateIsRequiredAndItsReservationFollowsAPartialFill) {
    HftRiskGate gate(1);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e9).maxPositionQuantity(1000).priceCollarPct(0.05);
    PaperTradingGateway gw(1e6, 0, gate, {{"X", 0}}, Sim::Config::zeroLatency());
    gw.onQuote("X", 1, 99, 100, 100, 100);
    std::int64_t id = gw.submitMarket("X", Side::BUY, 300);
    EXPECT_TRUE(gw.filledQuantity(id) == 100);
    EXPECT_TRUE(gate.position(0) == 100);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);   // the remainder went back

    std::int64_t resting = gw.submitLimit("X", Side::BUY, 50, 99);
    gw.onTrade("X", 2, 99, 120);                         // 100 ahead, 20 reach us
    EXPECT_TRUE(gw.status(resting) == OrderStatus::PARTIALLY_FILLED);
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 30);
    EXPECT_TRUE(gw.cancel(resting));
    EXPECT_TRUE(gate.openQuantity(0, Side::BUY) == 0);
    EXPECT_TRUE(gw.status(resting) == OrderStatus::CANCELED);
    EXPECT_TRUE(gw.filledQuantity(resting) == 20);
}

TEST(paperDefaultsApplyLatency) {
    auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1e6);
    const auto lat = PaperTradingGateway::FillConfig().orderLatencyNanos;
    gw.onQuote("X", 1'000'000, 99, 100, 100, 100);
    gw.onQuote("X", 2'000'000, 99, 100, 100, 100);       // now visible
    std::int64_t id = gw.submitMarket("X", Side::BUY, 10);
    EXPECT_TRUE(gw.status(id) == OrderStatus::NEW);      // in flight
    gw.onQuote("X", 2'000'000 + lat - 1, 104, 100, 105, 100);
    EXPECT_TRUE(gw.status(id) == OrderStatus::NEW);      // lands next, at the new price
    gw.onQuote("X", 2'000'000 + 2 * lat, 104, 100, 105, 100);
    EXPECT_TRUE(gw.status(id) == OrderStatus::FILLED);
    EXPECT_NEAR(gw.cash(), 1e6 - 10 * 105.0, 1e-9);      // the arrival price, not the send price
}

TEST(paperValidatesQuantityBeforeAnythingElse) {
    auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1e6, 0, Sim::Config::zeroLatency());
    EXPECT_TRUE(gw.status(gw.submitMarket("NOQUOTE", Side::BUY, 0)) == OrderStatus::REJECTED);
    EXPECT_TRUE(gw.rejectionLog()[0].find("INVALID_QUANTITY") != std::string::npos);
}

TEST(paperFeedOutOfOrderIsLoggedAndIgnored) {
    auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1e6, 0, Sim::Config::zeroLatency());
    gw.onQuote("X", 10, 99, 100, 100, 100);
    gw.onQuote("X", 9, 50, 100, 51, 100);
    gw.onTrade("X", 9, 99, 10);
    gw.onQuote("X", 11, 99, -1, 100, 100);
    EXPECT_TRUE(gw.snapshot().rejectionCount == 3);
}

TEST(paperRunRepeatsAndReportsMarkouts) {
    auto run = []() {
        Sim::Config c = Sim::Config::zeroLatency();
        c.markoutHorizonNanos = 10;
        auto gw = PaperTradingGateway::unsafeWithoutRiskGate(1e6, 0, c);
        std::vector<std::int64_t> stamps;
        gw.addExecutionListener([&](std::int64_t, const std::string&, Side, double, std::int64_t q,
                                    std::int64_t ts) { stamps.push_back(ts * 1000 + q); });
        gw.onQuote("X", 1, 99, 100, 100, 100);
        gw.submitMarket("X", Side::BUY, 10);
        gw.onQuote("X", 20, 89, 100, 91, 100);
        EXPECT_NEAR(gw.adverseSelection("X").aggressive.markout, (90 - 100) * 10.0, 1e-9);
        return stamps;
    };
    EXPECT_TRUE(run() == run());
}

// ------------------------------------------------------------ backtest seam

TEST(backtestModelFillsOnlyDepthAndCarriesTheRest) {
    TapeEvent book1;
    book1.nanos = 100;
    book1.bid = 99;
    book1.bidSize = 40;
    book1.ask = 100;
    book1.askSize = 40;
    TapeEvent book2 = book1;
    book2.nanos = 2'000'000'000;   // past depth recovery
    std::map<int, std::vector<TapeEvent>> tape{{1, {book1}}, {2, {book2}}};
    QueueExecution model(tape, QueueExecution::Style::AGGRESSIVE, 10, Sim::Config::zeroLatency());
    BarSeries series = BarSeries::of("X", {100, 100, 100});

    EXPECT_TRUE(model.execute(Side::BUY, 100, series, 0).empty());   // no tape, no fill
    auto fills = model.execute(Side::BUY, 100, series, 1);
    EXPECT_TRUE(fills.size() == 1 && fills[0].quantity == 40);
    EXPECT_NEAR(fills[0].price, 100 * 1.001, 1e-9);                  // all-in
    EXPECT_TRUE(model.execute(Side::BUY, 60, series, 2)[0].quantity == 40);
}

TEST(backtestPassiveModelWaitsForTheQueue) {
    TapeEvent book;
    book.nanos = 100;
    book.bid = 99;
    book.bidSize = 40;
    book.ask = 100;
    book.askSize = 40;
    TapeEvent trade;
    trade.nanos = 200;
    trade.isTrade = true;
    trade.tradePrice = 99;
    trade.tradeQty = 50;
    std::map<int, std::vector<TapeEvent>> tape{{0, {book, trade}}};
    QueueExecution model(tape, QueueExecution::Style::PASSIVE, 0, Sim::Config::zeroLatency());
    auto fills = model.execute(Side::BUY, 100, BarSeries::of("X", {99.5}), 0);
    EXPECT_TRUE(fills.size() == 1 && fills[0].quantity == 10);
    EXPECT_NEAR(fills[0].price, 99, 1e-12);
}

QTEST_MAIN()
