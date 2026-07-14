// Port of the TradeAnalytics halves of BacktestAnalyticsRoundTest.java and
// TradeAnalyticsEdgeTest.java: hand-pinned trade statistics, streak and
// scratch handling, the no-loser/all-loss Kelly degeneracies, and the
// expectancy = mean-P&L identity. Same pins, same tolerances.
#include "quantfinlib/backtest/trade_analytics.hpp"

#include <cmath>
#include <vector>

#include "qtest.hpp"

using quantfinlib::Trade;
namespace ta = quantfinlib::trade_analytics;

namespace {
Trade trade(double pnl, int bars) {
    return Trade{"X", 0, bars, 0, bars, 100, 100 + pnl, 1, pnl,
                 pnl / 100, Trade::REASON_SIGNAL};
}
}  // namespace

TEST(tradeStatisticsMatchHandArithmetic) {
    // 3 wins (+100,+50,+50 over 2/4/6 bars), 2 losses (-40,-60 over 8/10).
    std::vector<Trade> trades = {
            trade(100, 2), trade(-40, 8), trade(50, 4), trade(-60, 10), trade(50, 6)};
    ta::Result r = ta::analyze(trades);

    EXPECT_TRUE(r.count == 5);
    EXPECT_NEAR(r.winRate, 0.6, 1e-12);
    EXPECT_NEAR(r.avgWin, 200.0 / 3, 1e-12);            // 200/3
    EXPECT_NEAR(r.avgLoss, 50.0, 1e-12);                // 100/2
    // expectancy = 0.6*(200/3) - 0.4*50 = 40 - 20 = 20
    EXPECT_NEAR(r.expectancy, 0.6 * (200.0 / 3) - 0.4 * 50, 1e-12);
    EXPECT_NEAR(r.payoffRatio, (200.0 / 3) / 50.0, 1e-12);
    // Kelly = W - (1-W)/R = 0.6 - 0.4/(4/3) = 0.3.
    EXPECT_NEAR(r.kellyFraction, 0.6 - 0.4 / ((200.0 / 3) / 50.0), 1e-12);
    // Winners held 2,4,6 -> avg 4; losers 8,10 -> avg 9.
    EXPECT_NEAR(r.avgBarsHeldWinners, 4.0, 1e-12);
    EXPECT_NEAR(r.avgBarsHeldLosers, 9.0, 1e-12);
}

TEST(streaksAndScratchTradeHandling) {
    // W W L L L (scratch) W : max win streak 2, max loss streak 3; the
    // scratch trade breaks both streaks.
    std::vector<Trade> trades = {
            trade(10, 1), trade(10, 1), trade(-5, 1), trade(-5, 1), trade(-5, 1),
            trade(0, 1), trade(10, 1)};
    ta::Result r = ta::analyze(trades);
    EXPECT_TRUE(r.maxWinStreak == 2);
    EXPECT_TRUE(r.maxLossStreak == 3);
    // A scratch trade is neither a win nor a loss.
    EXPECT_NEAR(r.winRate, 3.0 / 7, 1e-12);
}

TEST(noLosersGivesInfinitePayoffAndFullKelly) {
    std::vector<Trade> trades = {trade(10, 1), trade(20, 1), trade(5, 1)};
    ta::Result r = ta::analyze(trades);
    EXPECT_TRUE(std::isinf(r.payoffRatio) && r.payoffRatio > 0);
    // No losers -> bet everything (the over-fit tell).
    EXPECT_NEAR(r.kellyFraction, 1.0, 0.0);
    EXPECT_THROWS(ta::analyze(std::vector<Trade>{}));
}

TEST(allScratchTradesProduceTheDocumentedNoLoserDegeneracy) {
    // Every trade breaks even: no wins, no losses. avgLoss == 0 takes the
    // documented "no losers" branch, so payoff is +inf and Kelly clamps
    // to 1 even though there are no winners either.
    ta::Result r = ta::analyze({trade(0, 1), trade(0, 2), trade(0, 3)});
    EXPECT_TRUE(r.count == 3);
    EXPECT_NEAR(r.winRate, 0.0, 0.0);
    EXPECT_NEAR(r.avgWin, 0.0, 0.0);
    EXPECT_NEAR(r.avgLoss, 0.0, 0.0);
    EXPECT_NEAR(r.expectancy, 0.0, 0.0);
    EXPECT_TRUE(std::isinf(r.payoffRatio) && r.payoffRatio > 0);
    EXPECT_NEAR(r.kellyFraction, 1.0, 0.0);
    // Scratches break streaks and belong to neither leg.
    EXPECT_TRUE(r.maxWinStreak == 0);
    EXPECT_TRUE(r.maxLossStreak == 0);
    EXPECT_NEAR(r.avgBarsHeldWinners, 0.0, 0.0);
    EXPECT_NEAR(r.avgBarsHeldLosers, 0.0, 0.0);
}

TEST(allLossesClampKellyToZero) {
    // W = 0 and R = 0: Kelly = 0 - 1/0 = -inf, clamped to 0 — never bet
    // a strategy that only loses.
    ta::Result r = ta::analyze({trade(-10, 2), trade(-20, 4), trade(-30, 6)});
    EXPECT_NEAR(r.winRate, 0.0, 0.0);
    EXPECT_NEAR(r.avgLoss, 20.0, 1e-12);           // (10+20+30)/3
    EXPECT_NEAR(r.payoffRatio, 0.0, 0.0);
    EXPECT_NEAR(r.kellyFraction, 0.0, 0.0);
    EXPECT_TRUE(r.maxLossStreak == 3);
    EXPECT_TRUE(r.maxWinStreak == 0);
    EXPECT_NEAR(r.expectancy, -20.0, 1e-12);       // 0*0 - 1*20
    // The single-loss record is the same story.
    ta::Result single = ta::analyze({trade(-50, 1)});
    EXPECT_NEAR(single.kellyFraction, 0.0, 0.0);
    EXPECT_TRUE(single.maxLossStreak == 1);
}

TEST(expectancyIsExactlyTheMeanPnlIncludingScratches) {
    // winRate*avgWin - lossRate*avgLoss = (winSum - lossSum)/n, i.e. the
    // plain average P&L per trade with scratches diluting it:
    // (+30 - 10 + 0)/3 = 20/3.
    ta::Result r = ta::analyze({trade(30, 1), trade(-10, 1), trade(0, 1)});
    EXPECT_NEAR(r.expectancy, 20.0 / 3, 1e-12);
}

TEST(nonFinitePnlIsRejected) {
    std::vector<Trade> bad = {trade(10, 1), trade(std::nan(""), 1)};
    EXPECT_THROWS(ta::analyze(bad));
}

QTEST_MAIN()
