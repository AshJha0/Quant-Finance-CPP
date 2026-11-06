// Phase 2B engine tests: Backtester core semantics (fills, commission,
// stop-loss / take-profit gap fills, force-close, warm-start), the five
// shipped strategies, and TradeCostModel. All equity pins are
// hand-derived from the documented fill rules on tiny synthetic series:
// SMA(2,3) on [10 x4, 20 x3, 5 x3] crosses up exactly once at bar 4 and
// down exactly once at bar 7.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/strategies/bollinger_bands_strategy.hpp"
#include "quantfinlib/backtest/strategies/ema_cross_strategy.hpp"
#include "quantfinlib/backtest/strategies/macd_strategy.hpp"
#include "quantfinlib/backtest/strategies/rsi_strategy.hpp"
#include "quantfinlib/backtest/strategies/sma_cross_strategy.hpp"
#include "quantfinlib/backtest/trade_cost_model.hpp"
#include "quantfinlib/data/bar_series.hpp"

using namespace quantfinlib;

namespace {

const std::vector<double> kCrossCloses = {10, 10, 10, 10, 20, 20, 20, 5, 5, 5};

BarSeries crossSeries() {
    return BarSeries::of("TEST", kCrossCloses);
}

BacktestConfig zeroCost() {
    return BacktestConfig{100000, 0.0, 0.0, 0.0, 0.0, 252}.withSameBarCloseFill().withoutVolumeCap();
}

/** Emits BUY at one fixed bar and never sells (force-close exercises). */
class BuyAtBar final : public TradingStrategy {
public:
    explicit BuyAtBar(int bar) : bar_(bar) {}
    std::string name() const override { return "BUY_AT_BAR"; }
    void init(const BarSeries&) override {}
    Signal onBar(int i) override { return i == bar_ ? Signal::BUY : Signal::HOLD; }

private:
    int bar_;
};

}  // namespace

// ------------------------------------------------------------ Backtester

TEST(smaCrossExactTradeCountAndFinalEquityZeroCost) {
    // BUY bar4 @20 (qty 5000), SELL bar7 @5: final equity 25000 exactly.
    SmaCrossStrategy strategy(2, 3);
    BarSeries series = crossSeries();
    BacktestResult result = backtester::run(strategy, series, zeroCost());

    EXPECT_TRUE(result.trades().size() == 1);
    const Trade& t = result.trades()[0];
    EXPECT_TRUE(t.entryIndex == 4);
    EXPECT_TRUE(t.exitIndex == 7);
    EXPECT_NEAR(t.entryPrice, 20.0, 1e-12);
    EXPECT_NEAR(t.exitPrice, 5.0, 1e-12);
    EXPECT_NEAR(t.quantity, 5000.0, 1e-9);
    EXPECT_NEAR(t.pnl, -75000.0, 1e-9);
    EXPECT_TRUE(t.exitReason == Trade::REASON_SIGNAL);
    EXPECT_TRUE(!t.isWin());
    EXPECT_TRUE(t.barsHeld() == 3);
    EXPECT_NEAR(result.equityCurve().back(), 25000.0, 1e-9);
    EXPECT_NEAR(result.metrics().finalEquity, 25000.0, 1e-9);
    EXPECT_TRUE(result.metrics().tradeCount == 1);
}

TEST(commissionChargedOnBothEntryAndExitNotional) {
    // Entry fee 100 -> qty = 99900/20 = 4995; exit proceeds 24975,
    // exit fee 24.975 -> final cash 24950.025.
    SmaCrossStrategy strategy(2, 3);
    BarSeries series = crossSeries();
    BacktestResult result =
            backtester::run(strategy, series, zeroCost().withCommission(0.001));

    EXPECT_TRUE(result.trades().size() == 1);
    EXPECT_NEAR(result.trades()[0].quantity, 4995.0, 1e-9);
    EXPECT_NEAR(result.equityCurve().back(), 24950.025, 1e-9);
    EXPECT_NEAR(result.trades()[0].pnl, 24950.025 - 100000.0, 1e-9);
    EXPECT_NEAR(result.trades()[0].returnPct, (24950.025 - 100000.0) / 100000.0, 1e-12);
}

TEST(slippageWorsensBothFills) {
    // Slippage 1%: entry fill 20.2, exit fill 4.95.
    SmaCrossStrategy strategy(2, 3);
    BarSeries series = crossSeries();
    BacktestConfig config = zeroCost();
    config.slippageRate = 0.01;
    BacktestResult result = backtester::run(strategy, series, config);

    EXPECT_TRUE(result.trades().size() == 1);
    EXPECT_NEAR(result.trades()[0].entryPrice, 20.2, 1e-12);
    EXPECT_NEAR(result.trades()[0].exitPrice, 4.95, 1e-12);
}

TEST(stopLossFillsIntrabarGapAware) {
    // Entry bar4 @20; stop 25% -> 15. Bar 6 trades at 5: the gap goes
    // straight through the level, so the fill is the OPEN (5), not 15.
    std::vector<double> closes = {10, 10, 10, 10, 20, 20, 5, 5};
    BarSeries series = BarSeries::of("TEST", closes);
    SmaCrossStrategy strategy(2, 3);
    BacktestResult result = backtester::run(strategy, series, zeroCost().withStopLoss(0.25));

    EXPECT_TRUE(result.trades().size() == 1);
    const Trade& t = result.trades()[0];
    EXPECT_TRUE(t.exitReason == Trade::REASON_STOP_LOSS);
    EXPECT_TRUE(t.exitIndex == 6);
    EXPECT_NEAR(t.exitPrice, 5.0, 1e-12);
    EXPECT_NEAR(result.equityCurve().back(), 25000.0, 1e-9);
}

TEST(takeProfitFillsAtOpenWhenGappedThrough) {
    // Entry bar4 @20; target 50% -> 30. Bar 5 opens at 40 (gap through
    // the level): fill = max(30, open) = 40.
    std::vector<double> closes = {10, 10, 10, 10, 20, 40, 40};
    BarSeries series = BarSeries::of("TEST", closes);
    SmaCrossStrategy strategy(2, 3);
    BacktestResult result = backtester::run(strategy, series, zeroCost().withTakeProfit(0.5));

    EXPECT_TRUE(result.trades().size() == 1);
    const Trade& t = result.trades()[0];
    EXPECT_TRUE(t.exitReason == Trade::REASON_TAKE_PROFIT);
    EXPECT_TRUE(t.exitIndex == 5);
    EXPECT_NEAR(t.exitPrice, 40.0, 1e-12);
    EXPECT_NEAR(result.equityCurve().back(), 200000.0, 1e-9);
}

TEST(openPositionForceClosedAtEndOfData) {
    std::vector<double> closes = {10, 10, 10, 10};
    BarSeries series = BarSeries::of("TEST", closes);
    BuyAtBar strategy(1);
    BacktestResult result = backtester::run(strategy, series, zeroCost());

    EXPECT_TRUE(result.trades().size() == 1);
    EXPECT_TRUE(result.trades()[0].exitReason == Trade::REASON_END_OF_DATA);
    EXPECT_TRUE(result.trades()[0].exitIndex == 3);
    EXPECT_NEAR(result.equityCurve().back(), 100000.0, 1e-9);
}

TEST(strategyStopLossOverridesConfigZero) {
    // Strategy-level stop beats a zero config stop (the Java precedence).
    class StoppedBuyer final : public TradingStrategy {
    public:
        std::string name() const override { return "STOPPED"; }
        void init(const BarSeries&) override {}
        Signal onBar(int i) override { return i == 0 ? Signal::BUY : Signal::HOLD; }
        double stopLossPct() const override { return 0.10; }
    };
    std::vector<double> closes = {100, 100, 80, 80};
    BarSeries series = BarSeries::of("TEST", closes);
    StoppedBuyer strategy;
    BacktestResult result = backtester::run(strategy, series, zeroCost());

    EXPECT_TRUE(result.trades().size() == 1);
    EXPECT_TRUE(result.trades()[0].exitReason == Trade::REASON_STOP_LOSS);
    EXPECT_TRUE(result.trades()[0].exitIndex == 2);
}

// ----------------------------------------------------- warm-start run()

TEST(warmStartTradesWhereColdSliceStillHolds) {
    // Warm run (full-series indicators, tradeFrom=3) catches the bar-4
    // cross; a cold backtest of slice(3, n) is still in indicator
    // warm-up there and never trades. This is the walk-forward
    // cold-start bias the tradeFrom overload exists to remove.
    std::vector<double> closes = {10, 10, 10, 10, 20, 20, 20};
    BarSeries series = BarSeries::of("TEST", closes);

    SmaCrossStrategy warm(2, 3);
    BacktestResult warmResult = backtester::run(warm, series, zeroCost(), 3);
    EXPECT_TRUE(warmResult.trades().size() == 1);
    EXPECT_TRUE(warmResult.trades()[0].entryIndex == 4);
    EXPECT_TRUE(warmResult.equityCurve().size() == 4);  // [tradeFrom, n)

    SmaCrossStrategy cold(2, 3);
    BarSeries coldSlice = series.slice(3, series.size());
    BacktestResult coldResult = backtester::run(cold, coldSlice, zeroCost());
    EXPECT_TRUE(coldResult.trades().empty());
}

TEST(warmStartEquityCurveMatchesFullRunTail) {
    // With no position open before tradeFrom, the warm run's equity is
    // the tail of the full run's equity.
    SmaCrossStrategy full(2, 3);
    BarSeries series = crossSeries();
    BacktestResult fullResult = backtester::run(full, series, zeroCost());

    SmaCrossStrategy warm(2, 3);
    BacktestResult warmResult = backtester::run(warm, series, zeroCost(), 2);
    EXPECT_TRUE(warmResult.equityCurve().size() == 8);
    for (std::size_t i = 0; i < warmResult.equityCurve().size(); i++) {
        EXPECT_NEAR(warmResult.equityCurve()[i], fullResult.equityCurve()[i + 2], 1e-9);
    }
}

TEST(backtesterGatesTradeFrom) {
    SmaCrossStrategy strategy(2, 3);
    BarSeries series = crossSeries();
    EXPECT_THROWS(backtester::run(strategy, series, zeroCost(), -1));
    EXPECT_THROWS(backtester::run(strategy, series, zeroCost(), series.size()));
}

// ------------------------------------------------------------ strategies

TEST(strategyNamesAndGates) {
    EXPECT_TRUE(SmaCrossStrategy(2, 3).name() == "SMA_CROSS(2,3)");
    EXPECT_TRUE(EmaCrossStrategy(5, 10).name() == "EMA_CROSS(5,10)");
    EXPECT_TRUE(MacdStrategy().name() == "MACD(12,26,9)");
    EXPECT_TRUE(RsiStrategy(14, 30, 70).name() == "RSI(14,30.0,70.0)");
    EXPECT_TRUE(BollingerBandsStrategy().name() == "BOLLINGER(20,2.0)");
    EXPECT_THROWS(SmaCrossStrategy(5, 5));
    EXPECT_THROWS(EmaCrossStrategy(10, 5));
}

TEST(trendStrategiesTradeASineWave) {
    // 200-bar sine (period 40): every crossover strategy must complete a
    // round trip; every trade exits after it enters.
    std::vector<double> closes(200);
    for (int i = 0; i < 200; i++) {
        closes[static_cast<std::size_t>(i)] = 100 + 20 * std::sin(2 * 3.141592653589793 * i / 40.0);
    }
    BarSeries series = BarSeries::of("SINE", closes);

    SmaCrossStrategy sma(5, 15);
    EmaCrossStrategy ema(5, 15);
    MacdStrategy macd;
    RsiStrategy rsi(5, 30, 70);
    TradingStrategy* strategies[] = {&sma, &ema, &macd, &rsi};
    for (TradingStrategy* s : strategies) {
        BacktestResult result = backtester::run(*s, series, zeroCost());
        EXPECT_TRUE(result.trades().size() >= 1);
        for (const Trade& t : result.trades()) {
            EXPECT_TRUE(t.exitIndex >= t.entryIndex);
        }
        EXPECT_TRUE(result.equityCurve().back() > 0);
    }
}

TEST(bollingerBuysBelowLowerBand) {
    // 20 flat bars then a crash: the crash close sits far below the
    // lower band -> BUY on that bar.
    std::vector<double> closes(21, 10.0);
    closes[20] = 5.0;
    BarSeries series = BarSeries::of("BB", closes);
    BollingerBandsStrategy strategy(20, 2.0);
    strategy.init(series);
    for (int i = 0; i < 20; i++) {
        EXPECT_TRUE(strategy.onBar(i) == Signal::HOLD);
    }
    EXPECT_TRUE(strategy.onBar(20) == Signal::BUY);
}

// --------------------------------------------------------- TradeCostModel

TEST(flatCostModelIsSizeAndBarIndependent) {
    TradeCostModel model = trade_cost_model::flat(25);
    BarSeries series = crossSeries();
    EXPECT_NEAR(model(series, 0, 1000.0), 25.0, 1e-12);
    EXPECT_NEAR(model(series, 9, 1e9), 25.0, 1e-12);
    EXPECT_THROWS(trade_cost_model::flat(-1));
}

TEST(institutionalChargesFlatComponentsBeforeImpactWindow) {
    // Constant closes -> zero vol -> zero impact even past the window:
    // the model returns exactly commission + halfSpread + slippage.
    std::vector<double> closes(10, 100.0);
    BarSeries series = BarSeries::of("FLATVOL", closes);
    TradeCostModel model = trade_cost_model::institutional(1, 2, 3, 4);
    EXPECT_NEAR(model(series, 1, 50000.0), 6.0, 1e-12);   // before window
    EXPECT_NEAR(model(series, 8, 50000.0), 6.0, 1e-12);   // vol = 0 past it
    EXPECT_THROWS(trade_cost_model::institutional(-1, 0, 0, 4));
    EXPECT_THROWS(trade_cost_model::institutional(0, 0, 0, 1));
}

TEST(institutionalImpactGrowsWithNotional) {
    // Volatile closes with volume: bigger tickets cost strictly more —
    // the square-root impact term is what prices capacity.
    BarSeries::Builder b = BarSeries::builder("VOL");
    double px = 100;
    for (int i = 0; i < 12; i++) {
        px *= (i % 2 == 0) ? 1.02 : 0.99;
        b.add(i * 86400000LL, px, px, px, px, 10000);
    }
    BarSeries series = b.build();
    TradeCostModel model = trade_cost_model::institutional(1, 2, 3, 5);
    double small = model(series, 10, 1000.0);
    double large = model(series, 10, 1000000.0);
    EXPECT_TRUE(small > 6.0);
    EXPECT_TRUE(large > small);
}

TEST(institutionalFallsBackToFlatWithoutVolume) {
    BarSeries::Builder b = BarSeries::builder("NOVOL");
    for (int i = 0; i < 10; i++) {
        b.add(i * 86400000LL, 100 + i, 100 + i, 100 + i, 100 + i, 0);
    }
    BarSeries series = b.build();
    TradeCostModel model = trade_cost_model::institutional(1, 2, 3, 4);
    EXPECT_NEAR(model(series, 8, 1e6), 6.0, 1e-12);
}

QTEST_MAIN()
