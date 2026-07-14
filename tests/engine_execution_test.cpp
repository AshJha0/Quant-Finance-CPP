// Phase 2B engine tests: execution models and the ExecutionAwareBacktester.
// The CASH CONSERVATION checks replay every child fill in engine order and
// assert running cash never goes negative — the historical -1475 / -3950
// overdraw regressions (flat 1% buffer vs. a costlier model; close-based
// sizing vs. last-look's open-anchored fills) must NOT reproduce.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/backtest/execution_aware_backtester.hpp"
#include "quantfinlib/backtest/iceberg_execution.hpp"
#include "quantfinlib/backtest/instant_execution.hpp"
#include "quantfinlib/backtest/last_look_execution.hpp"
#include "quantfinlib/backtest/strategies/sma_cross_strategy.hpp"
#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/execution/iceberg_order.hpp"

using namespace quantfinlib;

namespace {

BacktestConfig zeroCost() {
    return BacktestConfig{100000, 0.0, 0.0, 0.0, 0.0, 252};
}

/** Scripted signals: BUY at one bar, optional SELL at another. */
class Script final : public TradingStrategy {
public:
    Script(int buyBar, int sellBar) : buyBar_(buyBar), sellBar_(sellBar) {}
    std::string name() const override { return "SCRIPT"; }
    void init(const BarSeries&) override {}
    Signal onBar(int i) override {
        if (i == buyBar_) return Signal::BUY;
        if (i == sellBar_) return Signal::SELL;
        return Signal::HOLD;
    }

private:
    int buyBar_;
    int sellBar_;
};

/**
 * Replays all child fills in engine order (parents are appended
 * chronologically; fills within a parent are bar-ordered) and returns
 * the minimum running cash seen after any fill.
 */
double minRunningCash(const ExecutionAwareResult& result, double initialCapital) {
    struct Item {
        int bar;
        std::size_t parentSeq;
        double delta;
    };
    std::vector<Item> items;
    for (std::size_t p = 0; p < result.parentOrders().size(); p++) {
        const ParentOrder& parent = result.parentOrders()[p];
        for (std::size_t f = 0; f < parent.fills.size(); f++) {
            double notional = parent.fills[f].notional();
            items.push_back(Item{parent.fillBarIndices[f], p,
                                 parent.side == Side::BUY ? -notional : notional});
        }
    }
    // Stable ordering by (bar, parent sequence): an entry parent is
    // always created before the exit parent that follows it.
    for (std::size_t i = 1; i < items.size(); i++) {
        Item key = items[i];
        std::size_t j = i;
        while (j > 0
               && (items[j - 1].bar > key.bar
                   || (items[j - 1].bar == key.bar && items[j - 1].parentSeq > key.parentSeq))) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
    double cash = initialCapital;
    double minCash = cash;
    for (const Item& it : items) {
        cash += it.delta;
        if (cash < minCash) {
            minCash = cash;
        }
    }
    return minCash;
}

}  // namespace

// -------------------------------------------------------- InstantExecution

TEST(instantExecutionFillsAllInAtClose) {
    std::vector<double> closes = {10, 10, 10, 10, 20, 20, 20, 5, 5, 5};
    BarSeries series = BarSeries::of("TEST", closes);
    BacktestConfig config = BacktestConfig::defaults();
    SmaCrossStrategy strategy(2, 3);
    InstantExecution model = InstantExecution::from(config);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    // Whole shares: floor(100000 / (20 * 1.001)) = 4995 at 20.02 all-in;
    // exit 4995 * 5 * 0.999 = 24950.025 -> final 24950.125.
    EXPECT_TRUE(result.backtest().trades().size() == 1);
    const Trade& t = result.backtest().trades()[0];
    EXPECT_NEAR(t.quantity, 4995.0, 1e-9);
    EXPECT_NEAR(t.entryPrice, 20.02, 1e-9);
    EXPECT_NEAR(t.exitPrice, 4.995, 1e-9);
    EXPECT_NEAR(result.backtest().equityCurve().back(), 24950.125, 1e-6);
    EXPECT_NEAR(minRunningCash(result, config.initialCapital), 0.1, 1e-6);

    // Two parents: the entry and the signal exit, both single-bar.
    EXPECT_TRUE(result.parentOrders().size() == 2);
    EXPECT_TRUE(result.parentOrders()[0].reason == std::string(ParentOrder::REASON_ENTRY));
    EXPECT_TRUE(result.parentOrders()[1].reason == std::string(Trade::REASON_SIGNAL));
    EXPECT_TRUE(result.parentOrders()[0].fillDurationBars() == 0);
}

TEST(forcedCloseChargesWorstCaseCost) {
    std::vector<double> closes = {10, 10, 10, 10};
    BarSeries series = BarSeries::of("TEST", closes);
    BacktestConfig config = zeroCost().withCommission(0.001);
    Script strategy(1, -1);
    InstantExecution model = InstantExecution::from(config);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    // Entry: floor(100000/10.01) = 9990 shares at 10.01 = 99999.9.
    // Forced close at 10 * 0.999 = 9.99 -> 99800.1 -> final 99800.2.
    EXPECT_TRUE(result.backtest().trades().size() == 1);
    const Trade& t = result.backtest().trades()[0];
    EXPECT_TRUE(t.exitReason == Trade::REASON_END_OF_DATA);
    EXPECT_NEAR(t.exitPrice, 9.99, 1e-12);
    EXPECT_NEAR(result.backtest().equityCurve().back(), 99800.2, 1e-6);
    EXPECT_TRUE(result.parentOrders().back().fills.back().venue == "FORCED_CLOSE");
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
}

// -------------------------------------------------------- IcebergExecution

TEST(icebergWorksEntryAcrossBarsAndConservesCash) {
    // Rising tape while an iceberg accumulates: every bar re-budgets at
    // that bar's price, so cash can never be overdrawn even though later
    // tranches fill higher than the signal close.
    std::vector<double> closes = {100, 105, 110, 115, 120, 125, 130, 135, 140, 100};
    BarSeries series = BarSeries::of("ICE", closes);
    BacktestConfig config = zeroCost().withCommission(0.001);
    Script strategy(0, 8);
    InstantExecution inner(config.commissionRate, config.slippageRate);
    IcebergExecution model(inner, 200);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    // Parent target = floor(100000 / (100 * 1.001)) = 999 shares. The
    // display tranche is 200 but every bar RE-BUDGETS at that bar's
    // price on the leftover cash, so the rising tape throttles the last
    // tranche: 200 @ 100.1, 200 @ 105.105, 200 @ 110.11, 200 @ 115.115
    // spend down to 13914, which only affords 115 shares at 120.12.
    const ParentOrder& entry = result.parentOrders()[0];
    EXPECT_TRUE(entry.fills.size() == 5);
    EXPECT_TRUE(entry.filledQty() == 915);
    EXPECT_TRUE(entry.fillDurationBars() == 4);
    EXPECT_TRUE(entry.fills[0].quantity == 200);
    EXPECT_TRUE(entry.fills[4].quantity == 115);
    // The overdraw regression: replayed cash must never go negative.
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
    // Exit parent worked 200/bar from bar 8; end of data forces the rest.
    EXPECT_TRUE(result.backtest().trades().size() == 1);
    EXPECT_TRUE(result.backtest().trades()[0].quantity == 915.0);
}

TEST(icebergParticipationCapThrottlesFills) {
    BarSeries::Builder b = BarSeries::builder("THIN");
    for (int i = 0; i < 10; i++) {
        b.add(i * 86400000LL, 100, 100, 100, 100, 1000);  // 1000 shares/bar
    }
    BarSeries series = b.build();
    BacktestConfig config = zeroCost();
    Script strategy(0, -1);
    InstantExecution inner(0, 0);
    // Display 500 but 10% of 1000 = 100 caps every tranche.
    IcebergExecution model(inner, 500, 0, 0.10, 42);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    const ParentOrder& entry = result.parentOrders()[0];
    for (const Execution& f : entry.fills) {
        EXPECT_TRUE(f.quantity <= 100);
    }
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
}

TEST(icebergDelegatesCostFractionToInner) {
    InstantExecution inner(0.02, 0.01);  // 3% all-in, way past the 1% default
    IcebergExecution model(inner, 100);
    EXPECT_NEAR(model.worstCaseCostFraction(), 0.03, 1e-12);
    EXPECT_THROWS(IcebergExecution(inner, 0));
}

TEST(icebergOrderStateMachineReloadsTranches) {
    IcebergOrder order(1000, 300);
    EXPECT_TRUE(order.visibleQty() == 300);
    EXPECT_TRUE(order.hiddenQty() == 700);
    EXPECT_TRUE(order.onFill(300));   // tranche exhausted -> reload
    EXPECT_TRUE(order.visibleQty() == 300);
    EXPECT_TRUE(!order.onFill(100));  // partial: no reload
    EXPECT_TRUE(order.remainingQty() == 600);
    EXPECT_THROWS(order.onFill(0));
    EXPECT_THROWS(order.onFill(500));  // exceeds visible
    EXPECT_THROWS(IcebergOrder(0, 10));
}

// ------------------------------------------------------- LastLookExecution

TEST(lastLookHoldsOnParentSignalBar) {
    // The parent is decided at the signal bar's CLOSE: filling at that
    // bar's open would be intrabar time travel. Hold, and count neither
    // a fill nor a reject.
    BarSeries::Builder b = BarSeries::builder("FX");
    b.add(0, 100, 101, 99, 100, 1e6);
    b.add(1, 100, 101, 99, 100, 1e6);
    BarSeries series = b.build();
    LastLookExecution model(20, 50);
    model.onParentOrder(Side::BUY, 100, 0);
    EXPECT_TRUE(model.execute(Side::BUY, 100, series, 0).empty());
    EXPECT_TRUE(model.fillCount() == 0);
    EXPECT_TRUE(model.rejectCount() == 0);

    std::vector<Execution> fills = model.execute(Side::BUY, 100, series, 1);
    EXPECT_TRUE(fills.size() == 1);
    EXPECT_NEAR(fills[0].price, 100 * (1 + 20.0 / 1e4), 1e-12);
    EXPECT_TRUE(model.fillCount() == 1);
}

TEST(lastLookRejectsMovesInTheTakersFavor) {
    BarSeries::Builder b = BarSeries::builder("FX");
    b.add(0, 100, 103, 100, 102, 1e6);   // +200 bps intra-bar: LP pulls a buy
    b.add(1, 100, 100.2, 99.5, 99.8, 1e6);  // benign bar: fill
    b.add(2, 100, 100.5, 97, 98, 1e6);   // -200 bps: favorable to a SELLER
    BarSeries series = b.build();
    LastLookExecution model(10, 50);

    EXPECT_TRUE(model.execute(Side::BUY, 100, series, 0).empty());
    EXPECT_TRUE(model.rejectCount() == 1);
    EXPECT_TRUE(model.execute(Side::BUY, 100, series, 1).size() == 1);
    // The same falling bar that rejects a seller is fine for a buyer.
    EXPECT_TRUE(model.execute(Side::SELL, 100, series, 2).empty());
    EXPECT_TRUE(model.rejectCount() == 2);
    EXPECT_TRUE(model.execute(Side::BUY, 100, series, 2).size() == 1);
    EXPECT_NEAR(model.rejectRate(), 2.0 / 4.0, 1e-12);
    EXPECT_THROWS(LastLookExecution(-1, 50));
    EXPECT_THROWS(LastLookExecution(10, 0));
}

TEST(lastLookGappedOpensCannotOverdrawCash) {
    // Strong gap-up tape: each open is 5% above the prior close. Sizing
    // against the signal close would overdraw on the first accepted
    // fill (the historical -3950 regression); referencePrice anchors the
    // budget to the fill bar's OPEN instead.
    BarSeries::Builder b = BarSeries::builder("GAP");
    double close = 100;
    for (int i = 0; i < 8; i++) {
        double open = close * 1.05;
        close = open * 1.002;  // +20 bps intra-bar, below the reject threshold
        b.add(i * 86400000LL, open, close, open, close, 1e6);
    }
    BarSeries series = b.build();
    BacktestConfig config = zeroCost();
    Script strategy(1, -1);
    LastLookExecution model(20, 100);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    // Signal bar 1 held; first fill on bar 2 at open(2) * (1 + spread).
    const ParentOrder& entry = result.parentOrders()[0];
    EXPECT_TRUE(!entry.fills.empty());
    EXPECT_TRUE(entry.fillBarIndices[0] == 2);
    EXPECT_NEAR(entry.fills[0].price, series.open(2) * (1 + 20.0 / 1e4), 1e-9);
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
}

// ------------------------------------------- ExecutionAwareBacktester flow

TEST(sellSignalCancelsUnfilledEntryRemainder) {
    // Iceberg fills 100/bar; a SELL on bar 2 cancels the remaining
    // entry and exits only what was accumulated.
    std::vector<double> closes = {100, 100, 100, 100, 100, 100};
    BarSeries series = BarSeries::of("CXL", closes);
    BacktestConfig config = zeroCost();
    Script strategy(0, 2);
    InstantExecution inner(0, 0);
    IcebergExecution model(inner, 100);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    // Entry filled bars 0,1,2 -> 300 shares; exit works 100/bar from
    // bar 2 and finishes by end of data.
    EXPECT_TRUE(result.backtest().trades().size() == 1);
    EXPECT_NEAR(result.backtest().trades()[0].quantity, 300.0, 1e-12);
    EXPECT_NEAR(result.backtest().equityCurve().back(), 100000.0, 1e-6);
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
}

TEST(riskExitWorkedThroughThePatientModel) {
    // Stop-loss triggers on bar 2; the iceberg exits slowly instead of
    // teleporting flat — that realism is the point.
    std::vector<double> closes = {20, 20, 5, 5, 5, 5, 5, 5};
    BarSeries series = BarSeries::of("STOP", closes);
    BacktestConfig config = zeroCost().withStopLoss(0.25);
    Script strategy(0, -1);
    InstantExecution inner(0, 0);
    IcebergExecution model(inner, 1000);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    EXPECT_TRUE(result.parentOrders().size() == 2);
    const ParentOrder& exit = result.parentOrders()[1];
    EXPECT_TRUE(exit.reason == std::string(Trade::REASON_STOP_LOSS));
    EXPECT_TRUE(exit.fillDurationBars() >= 1);  // 5000 shares at 1000/bar
    EXPECT_TRUE(minRunningCash(result, config.initialCapital) >= -1e-9);
}

TEST(parentOrderHelpers) {
    ParentOrder empty;
    EXPECT_TRUE(empty.filledQty() == 0);
    EXPECT_TRUE(std::isnan(empty.avgFillPrice()));
    EXPECT_TRUE(empty.fillDurationBars() == 0);

    ParentOrder p;
    p.fills.push_back(Execution{"S", Side::BUY, 10, 100, 0, "A"});
    p.fills.push_back(Execution{"S", Side::BUY, 20, 300, 1, "A"});
    p.fillBarIndices = {3, 7};
    EXPECT_TRUE(p.filledQty() == 400);
    EXPECT_NEAR(p.avgFillPrice(), (10.0 * 100 + 20.0 * 300) / 400, 1e-12);
    EXPECT_TRUE(p.fillDurationBars() == 4);
}

TEST(tcaReportsPerParentOrder) {
    std::vector<double> closes = {100, 105, 110, 115, 120, 125, 130, 135, 140, 100};
    BarSeries series = BarSeries::of("TCA", closes);
    BacktestConfig config = zeroCost().withCommission(0.001);
    Script strategy(0, 8);
    InstantExecution inner(config.commissionRate, 0);
    IcebergExecution model(inner, 200);
    ExecutionAwareResult result =
            ExecutionAwareBacktester::run(strategy, series, config, model);

    const ParentOrder& entry = result.parentOrders()[0];
    transaction_cost_analyzer::TcaReport report = result.tca(entry);
    EXPECT_TRUE(report.totalQuantity == entry.filledQty());
    // Rising tape while buying: implementation shortfall must be a cost.
    EXPECT_TRUE(report.implementationShortfallBps > 0);
    EXPECT_TRUE(!result.allFills().empty());

    ParentOrder unfilled;
    EXPECT_THROWS(result.tca(unfilled));
}

QTEST_MAIN()
