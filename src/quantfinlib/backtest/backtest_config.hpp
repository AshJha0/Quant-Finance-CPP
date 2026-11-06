// Port of com.quantfinlib.backtest.BacktestConfig (Java record): backtest
// execution parameters. Rates are fractions (0.001 = 10 bps).
// stopLossPct/takeProfitPct of 0 disable the respective check; a
// strategy's own stop/take-profit settings override zeros here. The
// record's withX copies are kept verbatim.
//
// DEVIATION FROM JAVA: the defaults are the honest ones. A signal computed
// from bar t's close fills at bar t+1's open, costs are non-zero, and each
// fill is capped at a share of the bar's volume. Filling at the bar that
// produced the signal needs withSameBarCloseFill(), which says what it is.
#pragma once

namespace quantfinlib {

/** When a signal decided on bar t's close reaches the market. */
enum class FillTiming {
    NEXT_BAR_OPEN,   // bar t+1's open: the earliest price a real order can get
    SAME_BAR_CLOSE   // bar t's close: assumes trading at the price that made the signal
};

struct BacktestConfig {
    double initialCapital = 0;
    double commissionRate = 0.001;
    double slippageRate = 0.0005;
    double stopLossPct = 0;
    double takeProfitPct = 0;
    int periodsPerYear = 252;
    FillTiming fillTiming = FillTiming::NEXT_BAR_OPEN;
    // Largest share of a bar's volume one fill may take; 0 disables. A
    // series with no volume at all has no cap (nothing to measure against);
    // a zero-volume bar in a series that has volume does not trade.
    double maxParticipationRate = 0.10;

    static BacktestConfig defaults() {
        BacktestConfig c;
        c.initialCapital = 100000;
        return c;
    }

    /** Opt in to filling at the signal bar's close. Flatters most strategies. */
    BacktestConfig withSameBarCloseFill() const {
        BacktestConfig c = *this;
        c.fillTiming = FillTiming::SAME_BAR_CLOSE;
        return c;
    }

    BacktestConfig withMaxParticipation(double rate) const {
        BacktestConfig c = *this;
        c.maxParticipationRate = rate;
        return c;
    }

    BacktestConfig withoutVolumeCap() const { return withMaxParticipation(0); }

    BacktestConfig withSlippage(double rate) const {
        BacktestConfig c = *this;
        c.slippageRate = rate;
        return c;
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
