// Port of RangeVolatilityTest and RangeVolatilityEdgeTest (Java): range
// estimators pinned two ways — constant-range synthetic bars match each
// estimator's closed form EXACTLY, and a discretely sampled GBM path puts
// every estimator near the true sigma with Parkinson demonstrably tighter
// than close-to-close. The Java BarSeries overload test is ported against
// the std::vector<Bar> overloads.
#include "quantfinlib/volatility/range_volatility.hpp"

#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "quantfinlib/data/bar.hpp"
#include "quantfinlib/util/math_utils.hpp"
#include "qtest.hpp"

using quantfinlib::Bar;
using quantfinlib::RangeVolatility;
namespace mu = quantfinlib::math_utils;

namespace {

const double PPY = 252;

std::vector<double> fill(int n, double v) {
    return std::vector<double>(n, v);
}

std::vector<double> scale4(const std::vector<double>& a) {
    std::vector<double> out(a.size());
    for (std::size_t i = 0; i < a.size(); i++) {
        out[i] = 4 * a[i];
    }
    return out;
}

}  // namespace

// ------------------------------------------------------- exact closed forms

TEST(parkinsonMatchesItsClosedFormOnConstantRangeBars) {
    int n = 10;
    std::vector<double> h = fill(n, 105);
    std::vector<double> l = fill(n, 100);
    // sigma^2 = mean(ln(H/L)^2)/(4 ln 2); constant bars make the mean exact.
    double lnHl = std::log(105.0 / 100.0);
    double expected = std::sqrt(lnHl * lnHl / (4 * std::log(2.0)) * PPY);
    EXPECT_NEAR(RangeVolatility::parkinson(h, l, PPY), expected, 1e-15);
}

TEST(garmanKlassAndRogersSatchellMatchTheirClosedFormsOnAConstantBar) {
    int n = 7;
    double o = 100, hi = 108, lo = 99, c = 104;
    std::vector<double> open = fill(n, o);
    std::vector<double> high = fill(n, hi);
    std::vector<double> low = fill(n, lo);
    std::vector<double> close = fill(n, c);

    // GK: 0.5*ln(H/L)^2 - (2 ln 2 - 1)*ln(C/O)^2 per bar.
    double hl = std::log(hi / lo);
    double co = std::log(c / o);
    double gkVar = 0.5 * hl * hl - (2 * std::log(2.0) - 1) * co * co;
    EXPECT_NEAR(RangeVolatility::garmanKlass(open, high, low, close, PPY),
                std::sqrt(gkVar * PPY), 1e-15);

    // RS: ln(H/C)ln(H/O) + ln(L/C)ln(L/O) per bar.
    double rsVar = std::log(hi / c) * std::log(hi / o)
            + std::log(lo / c) * std::log(lo / o);
    EXPECT_NEAR(RangeVolatility::rogersSatchell(open, high, low, close, PPY),
                std::sqrt(rsVar * PPY), 1e-15);
}

TEST(yangZhangCollapsesToWeightedRogersSatchellWithoutGapsOrDrift) {
    // O_i = C_{i-1} (no overnight gap) and C_i = O_i (no open-to-close
    // move): both sample variances are exactly zero, so YZ^2 must be
    // exactly (1 - k) * RS^2 — the k weighting pinned by hand.
    int n = 5;                       // m = 4 estimation periods
    std::vector<double> open = fill(n, 100);
    std::vector<double> high = fill(n, 110);
    std::vector<double> low = fill(n, 95);
    std::vector<double> close = fill(n, 100);
    double rs = std::log(110.0 / 100) * std::log(110.0 / 100)
            + std::log(95.0 / 100) * std::log(95.0 / 100);
    int m = n - 1;
    double k = 0.34 / (1.34 + (m + 1.0) / (m - 1.0));
    EXPECT_NEAR(RangeVolatility::yangZhang(open, high, low, close, PPY),
                std::sqrt((1 - k) * rs * PPY), 1e-15);
}

TEST(barOverloadsAgreeWithTheArrayForms) {
    std::vector<double> o = {100, 104, 101}, h = {108, 107, 106},
                        l = {99, 100, 98}, c = {104, 101, 105};
    std::vector<Bar> bars;
    for (int i = 0; i < 3; i++) {
        bars.emplace_back(i, o[i], h[i], l[i], c[i], 1000);
    }
    EXPECT_NEAR(RangeVolatility::parkinson(bars, PPY),
                RangeVolatility::parkinson(h, l, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::garmanKlass(bars, PPY),
                RangeVolatility::garmanKlass(o, h, l, c, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::rogersSatchell(bars, PPY),
                RangeVolatility::rogersSatchell(o, h, l, c, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::yangZhang(bars, PPY),
                RangeVolatility::yangZhang(o, h, l, c, PPY), 0.0);
}

// ----------------------------------------------- GBM sanity + efficiency

TEST(gbmBarsRecoverTheTrueSigmaAndParkinsonBeatsCloseToClose) {
    // Driftless GBM at sigma = 20%, 390 intraday steps per bar (the observed
    // high/low undershoot the continuous extremes by ~1-2% of vol at this
    // sampling — tolerances leave room for that bias). Java uses Random(7);
    // mt19937(7) supplies the same shape of path.
    double sigma = 0.20;
    int bars = 500, steps = 390;
    double dt = 1.0 / PPY, dts = dt / steps;
    std::mt19937 rng(7);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::vector<double> open(bars), high(bars), low(bars), close(bars);
    double p = 100;
    for (int i = 0; i < bars; i++) {
        open[i] = p;
        double hi = p, lo = p;
        for (int s = 0; s < steps; s++) {
            p *= std::exp(-0.5 * sigma * sigma * dts
                          + sigma * std::sqrt(dts) * gauss(rng));
            hi = std::max(hi, p);
            lo = std::min(lo, p);
        }
        high[i] = hi;
        low[i] = lo;
        close[i] = p;
    }
    EXPECT_NEAR(RangeVolatility::garmanKlass(open, high, low, close, PPY), sigma, 0.02);
    EXPECT_NEAR(RangeVolatility::rogersSatchell(open, high, low, close, PPY), sigma, 0.02);
    EXPECT_NEAR(RangeVolatility::yangZhang(open, high, low, close, PPY), sigma, 0.02);
    EXPECT_NEAR(RangeVolatility::parkinson(high, low, PPY), sigma, 0.03);

    // Efficiency: over 25 independent 20-bar blocks the Parkinson estimates
    // scatter visibly less than close-to-close estimates — the ~4.9x
    // variance advantage shows up as a smaller sample std.
    int blocks = 25, len = bars / blocks;
    std::vector<double> park(blocks), c2c(blocks);
    for (int b = 0; b < blocks; b++) {
        int from = b * len;
        std::vector<double> bh(high.begin() + from, high.begin() + from + len);
        std::vector<double> bl(low.begin() + from, low.begin() + from + len);
        park[b] = RangeVolatility::parkinson(bh, bl, PPY);
        double sum = 0;
        for (int i = from + 1; i < from + len; i++) {
            double r = std::log(close[i] / close[i - 1]);
            sum += r * r;
        }
        c2c[b] = std::sqrt(sum / (len - 1) * PPY);
    }
    EXPECT_TRUE(mu::stdDev(park) < mu::stdDev(c2c));
}

// ------------------------------------------------------------------ gates

TEST(gatesRefuseMalformedBars) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> ok = {100, 100};
    // Misaligned lengths.
    EXPECT_THROWS(RangeVolatility::parkinson({105}, {105, 100}, PPY));
    // H < L.
    EXPECT_THROWS(RangeVolatility::parkinson({99, 99}, ok, PPY));
    // L <= 0.
    EXPECT_THROWS(RangeVolatility::parkinson({105, 105}, {0, 100}, PPY));
    // NaN.
    EXPECT_THROWS(RangeVolatility::parkinson({nan, 105}, ok, PPY));
    // ppy <= 0.
    EXPECT_THROWS(RangeVolatility::parkinson({105, 105}, ok, 0));
    // Close above high.
    EXPECT_THROWS(RangeVolatility::garmanKlass({100}, {101}, {99}, {102}, PPY));
    // Open below low.
    EXPECT_THROWS(RangeVolatility::rogersSatchell({98}, {101}, {99}, {100}, PPY));
    // YZ needs >= 3 bars.
    EXPECT_THROWS(RangeVolatility::yangZhang(ok, {105, 105}, {99, 99}, ok, PPY));
}

// ------------------------------------------- degenerate & invariance pins

TEST(constantPriceBarsHaveExactlyZeroVolUnderEveryEstimator) {
    // O = H = L = C = 100 for every bar: every log ratio is ln(1) = 0,
    // both Yang-Zhang sample variances are 0/0-free zeros, so all four
    // estimators must return 0.0 exactly — no tolerance.
    int n = 5;
    std::vector<double> o = fill(n, 100);
    std::vector<double> h = fill(n, 100);
    std::vector<double> l = fill(n, 100);
    std::vector<double> c = fill(n, 100);
    EXPECT_NEAR(RangeVolatility::parkinson(h, l, PPY), 0.0, 0.0);
    EXPECT_NEAR(RangeVolatility::garmanKlass(o, h, l, c, PPY), 0.0, 0.0);
    EXPECT_NEAR(RangeVolatility::rogersSatchell(o, h, l, c, PPY), 0.0, 0.0);
    EXPECT_NEAR(RangeVolatility::yangZhang(o, h, l, c, PPY), 0.0, 0.0);
}

TEST(estimatorsAreScaleInvariant) {
    // Volatility is about RATIOS: quoting the same bars in cents instead of
    // dollars (scale by 4 — a power of two, so the division h/l is
    // bit-identical) cannot change any estimate.
    std::vector<double> o = {100, 104, 101, 103};
    std::vector<double> h = {108, 107, 106, 105};
    std::vector<double> l = {99, 100, 98, 101};
    std::vector<double> c = {104, 101, 105, 102};
    std::vector<double> o4 = scale4(o), h4 = scale4(h), l4 = scale4(l), c4 = scale4(c);
    EXPECT_NEAR(RangeVolatility::parkinson(h4, l4, PPY),
                RangeVolatility::parkinson(h, l, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::garmanKlass(o4, h4, l4, c4, PPY),
                RangeVolatility::garmanKlass(o, h, l, c, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::rogersSatchell(o4, h4, l4, c4, PPY),
                RangeVolatility::rogersSatchell(o, h, l, c, PPY), 0.0);
    EXPECT_NEAR(RangeVolatility::yangZhang(o4, h4, l4, c4, PPY),
                RangeVolatility::yangZhang(o, h, l, c, PPY), 0.0);
}

QTEST_MAIN()
