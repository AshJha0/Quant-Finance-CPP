// Port of com.quantfinlib.backtest.BacktestConfig (Java record): backtest
// execution parameters. Rates are fractions (0.001 = 10 bps).
// stopLossPct/takeProfitPct of 0 disable the respective check; a
// strategy's own stop/take-profit settings override zeros here. The
// record's withX copies are kept verbatim.
#pragma once

namespace quantfinlib {

struct BacktestConfig {
    double initialCapital = 0;
    double commissionRate = 0;
    double slippageRate = 0;
    double stopLossPct = 0;
    double takeProfitPct = 0;
    int periodsPerYear = 252;

    static BacktestConfig defaults() {
        return BacktestConfig{100000, 0.001, 0.0, 0.0, 0.0, 252};
    }

    BacktestConfig withInitialCapital(double capital) const {
        BacktestConfig c = *this;
        c.initialCapital = capital;
        return c;
    }

    BacktestConfig withCommission(double rate) const {
        BacktestConfig c = *this;
        c.commissionRate = rate;
        return c;
    }

    BacktestConfig withStopLoss(double pct) const {
        BacktestConfig c = *this;
        c.stopLossPct = pct;
        return c;
    }

    BacktestConfig withTakeProfit(double pct) const {
        BacktestConfig c = *this;
        c.takeProfitPct = pct;
        return c;
    }
};

}  // namespace quantfinlib
