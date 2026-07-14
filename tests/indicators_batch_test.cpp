// Port of IndicatorsTest (Java) plus the RSI/ATR pins from FormulaPinsTest.
// Java's TestData.gbmSeries uses SplittableRandom; tests/test_data.hpp
// supplies the same SHAPE of GBM bars via mt19937, and the assertions are
// the same property checks (ranges, orderings, NaN warm-ups). All
// hand-derived pins port exactly.
#include "quantfinlib/indicators/indicators.hpp"

#include <cmath>
#include <vector>

#include "quantfinlib/data/bar.hpp"
#include "qtest.hpp"
#include "test_data.hpp"

using quantfinlib::Bar;
using quantfinlib::Indicators;

TEST(smaKnownValues) {
    std::vector<double> out = Indicators::sma({1, 2, 3, 4, 5}, 3);
    EXPECT_TRUE(std::isnan(out[0]));
    EXPECT_TRUE(std::isnan(out[1]));
    // (1+2+3)/3 = 2, (2+3+4)/3 = 3, (3+4+5)/3 = 4.
    EXPECT_NEAR(out[2], 2.0, 1e-12);
    EXPECT_NEAR(out[3], 3.0, 1e-12);
    EXPECT_NEAR(out[4], 4.0, 1e-12);
    EXPECT_THROWS(Indicators::sma({1, 2, 3}, 0));
}

TEST(emaSeedsWithSmaAndConverges) {
    std::vector<double> flat(50, 10.0);
    std::vector<double> out = Indicators::ema(flat, 10);
    EXPECT_NEAR(out[9], 10.0, 1e-12);
    EXPECT_NEAR(out[49], 10.0, 1e-12);
    EXPECT_TRUE(std::isnan(out[8]));
}

TEST(wmaWeightsRecentMore) {
    std::vector<double> out = Indicators::wma({1, 2, 3}, 3);
    // (1*1 + 2*2 + 3*3) / 6 = 14/6.
    EXPECT_NEAR(out[2], 14.0 / 6, 1e-12);
}

TEST(rsiExtremesAndRange) {
    std::vector<double> up(30);
    for (int i = 0; i < 30; i++) {
        up[i] = 100 + i;
    }
    std::vector<double> rsi = Indicators::rsi(up, 14);
    EXPECT_NEAR(rsi[29], 100.0, 1e-9);   // all gains: RSI pegs at 100

    std::vector<double> closes =
            qtestdata::closes(qtestdata::gbmBars(300, 100, 0.05, 0.2, 1));
    std::vector<double> r = Indicators::rsi(closes, 14);
    for (std::size_t i = 14; i < r.size(); i++) {
        EXPECT_TRUE(r[i] >= 0 && r[i] <= 100);
    }
}

// Port of FormulaPinsTest.rsiWilderSmoothingAndAtrGapTermsPinned (RSI half).
TEST(rsiWilderSmoothingPinned) {
    // Diffs {+1, -0.5, +1}: avgGain 2/3, avgLoss 1/6 -> RS 4 -> RSI 80.
    // Next diff +0.5 under WILDER smoothing: avgGain (2/3*2 + 0.5)/3,
    // avgLoss (1/6*2)/3 -> RS 5.5 -> RSI 100 - 100/6.5 = 84.6154 (a plain
    // rolling mean gives 75 here).
    std::vector<double> rsi =
            Indicators::rsi({100, 101, 100.5, 101.5, 102, 101, 102.5}, 3);
    EXPECT_NEAR(rsi[3], 80.0, 1e-9);
    EXPECT_NEAR(rsi[4], 100 - 100 / 6.5, 1e-9);   // Wilder, not a rolling mean
}

// Port of FormulaPinsTest.rsiWilderSmoothingAndAtrGapTermsPinned (ATR half).
TEST(atrGapTermsPinned) {
    // Bars (9,10,8,9), (9,9.5,8.5,9), (7,7.5,6.5,7): the third bar GAPS
    // down, so its true range is |low - prevClose| = 2.5, not high-low = 1.
    std::vector<Bar> bars;
    bars.emplace_back(0, 9, 10, 8, 9, 100);
    bars.emplace_back(1, 9, 9.5, 8.5, 9, 100);
    bars.emplace_back(2, 7, 7.5, 6.5, 7, 100);
    std::vector<double> tr = Indicators::trueRange(bars);
    EXPECT_NEAR(tr[0], 2.0, 1e-12);
    EXPECT_NEAR(tr[1], 1.0, 1e-12);
    EXPECT_NEAR(tr[2], 2.5, 1e-12);   // the gap term |low - prevClose|
    std::vector<double> atr = Indicators::atr(bars, 2);
    EXPECT_NEAR(atr[1], 1.5, 1e-12);  // seed: mean of the first two TRs
    EXPECT_NEAR(atr[2], 2.0, 1e-12);  // Wilder: (1.5*1 + 2.5)/2 — hl-only gives 1.25
}

TEST(macdHistogramIsLineMinusSignal) {
    std::vector<double> close =
            qtestdata::closes(qtestdata::gbmBars(200, 100, 0.1, 0.2, 2));
    Indicators::Macd m = Indicators::macd(close, 12, 26, 9);
    std::size_t last = close.size() - 1;
    EXPECT_TRUE(!std::isnan(m.signal[last]));
    EXPECT_NEAR(m.histogram[last], m.line[last] - m.signal[last], 1e-12);
    // Warm-up: the signal is NaN before the slow EMA has seeded.
    EXPECT_TRUE(std::isnan(m.line[24]));
}

TEST(bollingerBandsBracketMiddle) {
    std::vector<double> close =
            qtestdata::closes(qtestdata::gbmBars(100, 100, 0, 0.3, 3));
    Indicators::Bollinger b = Indicators::bollinger(close, 20, 2);
    for (std::size_t i = 19; i < close.size(); i++) {
        EXPECT_TRUE(b.upper[i] >= b.middle[i]);
        EXPECT_TRUE(b.lower[i] <= b.middle[i]);
    }
    EXPECT_TRUE(std::isnan(b.upper[18]));
}

TEST(atrPositiveAfterWarmup) {
    std::vector<Bar> s = qtestdata::gbmBars(100, 100, 0.05, 0.25, 4);
    std::vector<double> atr = Indicators::atr(s, 14);
    EXPECT_TRUE(std::isnan(atr[12]));
    for (std::size_t i = 13; i < s.size(); i++) {
        EXPECT_TRUE(atr[i] > 0);
    }
}

TEST(adxWithinRange) {
    std::vector<Bar> s = qtestdata::gbmBars(300, 100, 0.1, 0.25, 5);
    Indicators::Adx adx = Indicators::adx(s, 14);
    std::size_t last = s.size() - 1;
    EXPECT_TRUE(!std::isnan(adx.adx[last]));
    EXPECT_TRUE(adx.adx[last] >= 0 && adx.adx[last] <= 100);
    EXPECT_TRUE(adx.plusDi[last] >= 0 && adx.minusDi[last] >= 0);
}

TEST(vwapObvCmfCciWilliamsRProduceValues) {
    std::vector<Bar> s = qtestdata::gbmBars(150, 100, 0.08, 0.2, 6);
    std::size_t last = s.size() - 1;
    EXPECT_TRUE(Indicators::vwap(s)[last] > 0);
    EXPECT_TRUE(!std::isnan(Indicators::obv(s)[last]));
    double cmf = Indicators::cmf(s, 20)[last];
    EXPECT_TRUE(cmf >= -1 && cmf <= 1);
    EXPECT_TRUE(!std::isnan(Indicators::cci(s, 20)[last]));
    double wr = Indicators::williamsR(s, 14)[last];
    EXPECT_TRUE(wr <= 0 && wr >= -100);
}

TEST(superTrendDirectionFollowsStrongTrend) {
    // Strongly rising market: direction should end bullish.
    std::vector<Bar> s = qtestdata::gbmBars(400, 100, 0.5, 0.08, 7);
    Indicators::SuperTrend st = Indicators::superTrend(s, 10, 3);
    EXPECT_TRUE(st.direction[s.size() - 1] == 1);
    EXPECT_TRUE(!std::isnan(st.value[s.size() - 1]));
}

TEST(ichimokuCloudSpansPresent) {
    std::vector<Bar> s = qtestdata::gbmBars(300, 100, 0.1, 0.2, 8);
    Indicators::Ichimoku ich = Indicators::ichimoku(s, 9, 26, 52);
    std::size_t last = s.size() - 1;
    EXPECT_TRUE(!std::isnan(ich.tenkan[last]));
    EXPECT_TRUE(!std::isnan(ich.kijun[last]));
    EXPECT_TRUE(!std::isnan(ich.senkouA[last]));
    EXPECT_TRUE(!std::isnan(ich.senkouB[last]));
    // chikou is close displaced backwards.
    EXPECT_NEAR(ich.chikou[last - 26], s[last].close(), 1e-12);
}

TEST(stochasticRsiWithinRange) {
    std::vector<double> close =
            qtestdata::closes(qtestdata::gbmBars(300, 100, 0.05, 0.25, 9));
    Indicators::StochRsi sr = Indicators::stochasticRsi(close, 14, 14, 3, 3);
    std::size_t last = close.size() - 1;
    EXPECT_TRUE(!std::isnan(sr.k[last]));
    EXPECT_TRUE(sr.k[last] >= 0 && sr.k[last] <= 100);
    EXPECT_TRUE(sr.d[last] >= 0 && sr.d[last] <= 100);
}

TEST(parabolicSarStaysOnCorrectSideInTrend) {
    std::vector<Bar> s = qtestdata::gbmBars(300, 100, 0.6, 0.06, 10);
    std::vector<double> sar = Indicators::parabolicSar(s, 0.02, 0.02, 0.2);
    EXPECT_TRUE(!std::isnan(sar[s.size() - 1]));
    // In a strong uptrend the last SAR should sit below the close.
    EXPECT_TRUE(sar[s.size() - 1] < s[s.size() - 1].close());
}

TEST(keltnerAndDonchianChannelsOrdered) {
    std::vector<Bar> s = qtestdata::gbmBars(120, 100, 0.05, 0.2, 11);
    std::size_t last = s.size() - 1;
    Indicators::Keltner k = Indicators::keltner(s, 20, 10, 2);
    EXPECT_TRUE(k.upper[last] > k.middle[last] && k.middle[last] > k.lower[last]);
    Indicators::Donchian d = Indicators::donchian(s, 20);
    EXPECT_TRUE(d.upper[last] >= d.middle[last] && d.middle[last] >= d.lower[last]);
}

TEST(rocAndMomentum) {
    std::vector<double> v = {100, 110, 121};
    // 121/100 - 1 = 21%; 121 - 100 = 21.
    EXPECT_NEAR(Indicators::roc(v, 2)[2], 21.0, 1e-9);
    EXPECT_NEAR(Indicators::momentum(v, 2)[2], 21.0, 1e-9);
}

TEST(rollingHelpersTrackWindowExtremesAndSpread) {
    std::vector<double> v = {3, 1, 4, 1, 5};
    std::vector<double> hi = Indicators::highest(v, 3);
    std::vector<double> lo = Indicators::lowest(v, 3);
    EXPECT_TRUE(std::isnan(hi[1]));
    EXPECT_NEAR(hi[2], 4.0, 0.0);
    EXPECT_NEAR(hi[4], 5.0, 0.0);
    EXPECT_NEAR(lo[3], 1.0, 0.0);
    // rollingStd over {1,4,1}: mean 2, population variance (1+4+1)/3 = 2.
    std::vector<double> sd = Indicators::rollingStd(v, 3);
    EXPECT_NEAR(sd[3], std::sqrt(2.0), 1e-12);
}

QTEST_MAIN()
