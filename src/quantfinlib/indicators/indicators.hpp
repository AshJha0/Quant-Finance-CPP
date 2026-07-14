// Port of com.quantfinlib.indicators.Indicators (Java): the batch technical
// indicator engine. All indicators operate on vectors and return vectors (or
// small structs of vectors) aligned with the input: index i of the output
// corresponds to bar i, with NaN for warm-up bars.
//
// The Java BarSeries parameters are ported as const std::vector<Bar>&
// (BarSeries itself has no C++ port; Bar does). Included: RSI (Wilder), SMA,
// EMA, WMA, VWAP, MACD, ATR, ADX, CCI, ROC, Momentum, OBV, CMF, SuperTrend,
// Ichimoku Cloud, Stochastic RSI, Williams %R, Parabolic SAR, Bollinger Bands
// (population stdev), Keltner Channel, Donchian Channel.
#pragma once

#include <vector>

#include "quantfinlib/data/bar.hpp"

namespace quantfinlib {

class Indicators {
public:
    Indicators() = delete;

    // ------------------------------------------------------------------
    // Result structs for multi-line indicators
    // ------------------------------------------------------------------

    struct Macd {
        std::vector<double> line;
        std::vector<double> signal;
        std::vector<double> histogram;
    };

    struct Bollinger {
        std::vector<double> upper;
        std::vector<double> middle;
        std::vector<double> lower;
    };

    struct Adx {
        std::vector<double> adx;
        std::vector<double> plusDi;
        std::vector<double> minusDi;
    };

    /** direction: +1 = uptrend (value is support), -1 = downtrend (value is resistance). */
    struct SuperTrend {
        std::vector<double> value;
        std::vector<int> direction;
    };

    struct Ichimoku {
        std::vector<double> tenkan;
        std::vector<double> kijun;
        std::vector<double> senkouA;
        std::vector<double> senkouB;
        std::vector<double> chikou;
    };

    struct StochRsi {
        std::vector<double> k;
        std::vector<double> d;
    };

    struct Keltner {
        std::vector<double> upper;
        std::vector<double> middle;
        std::vector<double> lower;
    };

    struct Donchian {
        std::vector<double> upper;
        std::vector<double> middle;
        std::vector<double> lower;
    };

    // ------------------------------------------------------------------
    // Moving averages
    // ------------------------------------------------------------------

    /** Simple moving average. */
    static std::vector<double> sma(const std::vector<double>& v, int period);

    /** Exponential moving average, seeded with the SMA of the first period values. */
    static std::vector<double> ema(const std::vector<double>& v, int period);

    /** Linearly weighted moving average. */
    static std::vector<double> wma(const std::vector<double>& v, int period);

    // ------------------------------------------------------------------
    // Momentum / oscillators
    // ------------------------------------------------------------------

    /** Relative Strength Index (Wilder smoothing). */
    static std::vector<double> rsi(const std::vector<double>& v, int period);

    /** Rate of change, percent: (v[i] / v[i-period] - 1) * 100. */
    static std::vector<double> roc(const std::vector<double>& v, int period);

    /** Momentum: v[i] - v[i-period]. */
    static std::vector<double> momentum(const std::vector<double>& v, int period);

    /** MACD: EMA(fast) - EMA(slow), with an EMA signal line and histogram. */
    static Macd macd(const std::vector<double>& close, int fastPeriod, int slowPeriod,
                     int signalPeriod);

    /** Stochastic RSI: stochastic oscillator applied to RSI, with %K and %D smoothing. */
    static StochRsi stochasticRsi(const std::vector<double>& close, int rsiPeriod,
                                  int stochPeriod, int kSmooth, int dSmooth);

    /** Williams %R: -100 * (highestHigh - close) / (highestHigh - lowestLow). */
    static std::vector<double> williamsR(const std::vector<Bar>& s, int period);

    /** Commodity Channel Index over typical price. */
    static std::vector<double> cci(const std::vector<Bar>& s, int period);

    // ------------------------------------------------------------------
    // Volatility / range
    // ------------------------------------------------------------------

    /** True range series. */
    static std::vector<double> trueRange(const std::vector<Bar>& s);

    /** Average True Range (Wilder smoothing). */
    static std::vector<double> atr(const std::vector<Bar>& s, int period);

    /** Average Directional Index with +DI / -DI (Wilder). */
    static Adx adx(const std::vector<Bar>& s, int period);

    /** Bollinger Bands: SMA middle band with k population standard deviations. */
    static Bollinger bollinger(const std::vector<double>& close, int period, double k);

    /** Keltner Channel: EMA middle band with ATR-based envelope. */
    static Keltner keltner(const std::vector<Bar>& s, int emaPeriod, int atrPeriod,
                           double multiplier);

    /** Donchian Channel: highest high / lowest low over the period. */
    static Donchian donchian(const std::vector<Bar>& s, int period);

    // ------------------------------------------------------------------
    // Volume
    // ------------------------------------------------------------------

    /** On-Balance Volume. */
    static std::vector<double> obv(const std::vector<Bar>& s);

    /** Cumulative Volume-Weighted Average Price (anchored at the series start). */
    static std::vector<double> vwap(const std::vector<Bar>& s);

    /** Chaikin Money Flow. */
    static std::vector<double> cmf(const std::vector<Bar>& s, int period);

    // ------------------------------------------------------------------
    // Trend systems
    // ------------------------------------------------------------------

    /** SuperTrend with ATR bands. */
    static SuperTrend superTrend(const std::vector<Bar>& s, int period, double multiplier);

    /**
     * Ichimoku Cloud with standard forward/backward displacement: senkou
     * spans are plotted kijunPeriod bars ahead, chikou kijunPeriod bars behind.
     */
    static Ichimoku ichimoku(const std::vector<Bar>& s, int tenkanPeriod, int kijunPeriod,
                             int senkouBPeriod);

    /** Parabolic SAR (standard Wilder acceleration schedule). */
    static std::vector<double> parabolicSar(const std::vector<Bar>& s, double afStart,
                                            double afStep, double afMax);

    // ------------------------------------------------------------------
    // Rolling helpers
    // ------------------------------------------------------------------

    static std::vector<double> highest(const std::vector<double>& v, int period);

    static std::vector<double> lowest(const std::vector<double>& v, int period);

    /** Rolling population standard deviation. */
    static std::vector<double> rollingStd(const std::vector<double>& v, int period);
};

}  // namespace quantfinlib
