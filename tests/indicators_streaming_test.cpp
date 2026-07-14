// Port of StreamingIndicatorsTest (Java): the streaming indicators must
// match the batch engine value-for-value (including NaN warm-up positions)
// so live behavior equals backtested behavior. The Java fixture is
// TestData.gbmSeries(..., seed 99); tests/test_data.hpp supplies the same
// shape of series via mt19937 — the equivalence assertions are exact either
// way because both sides consume the identical stream.
#include "quantfinlib/indicators/streaming_indicators.hpp"

#include <cmath>
#include <string>
#include <vector>

#include "quantfinlib/indicators/indicators.hpp"
#include "qtest.hpp"
#include "test_data.hpp"

using quantfinlib::Indicators;
using quantfinlib::StreamingIndicators;

namespace {

const std::vector<double>& fixtureCloses() {
    static const std::vector<double> closes =
            qtestdata::closes(qtestdata::gbmBars(500, 100, 0.08, 0.25, 99));
    return closes;
}

void expectSame(double expected, double actual, int index, const char* what) {
    if (std::isnan(expected)) {
        if (!std::isnan(actual)) {
            qtest::fail(__FILE__, __LINE__,
                        std::string(what) + " expected NaN at "
                                + std::to_string(index) + " but was "
                                + std::to_string(actual));
        }
    } else if (!(std::fabs(expected - actual) <= 1e-9)) {
        qtest::fail(__FILE__, __LINE__,
                    std::string(what) + " mismatch at " + std::to_string(index)
                            + ": " + std::to_string(expected) + " vs "
                            + std::to_string(actual));
    }
}

}  // namespace

TEST(smaMatchesBatch) {
    const auto& closes = fixtureCloses();
    std::vector<double> batch = Indicators::sma(closes, 20);
    StreamingIndicators::Sma s(20);
    for (int i = 0; i < static_cast<int>(closes.size()); i++) {
        expectSame(batch[i], s.update(closes[i]), i, "SMA");
    }
    expectSame(batch.back(), s.value(), static_cast<int>(closes.size()) - 1, "SMA value()");
}

TEST(emaMatchesBatch) {
    const auto& closes = fixtureCloses();
    std::vector<double> batch = Indicators::ema(closes, 20);
    StreamingIndicators::Ema e(20);
    for (int i = 0; i < static_cast<int>(closes.size()); i++) {
        expectSame(batch[i], e.update(closes[i]), i, "EMA");
    }
}

TEST(rsiMatchesBatch) {
    const auto& closes = fixtureCloses();
    std::vector<double> batch = Indicators::rsi(closes, 14);
    StreamingIndicators::Rsi r(14);
    for (int i = 0; i < static_cast<int>(closes.size()); i++) {
        expectSame(batch[i], r.update(closes[i]), i, "RSI");
    }
}

TEST(macdMatchesBatchIncludingSignalAndHistogram) {
    const auto& closes = fixtureCloses();
    Indicators::Macd batch = Indicators::macd(closes, 12, 26, 9);
    StreamingIndicators::Macd m(12, 26, 9);
    for (int i = 0; i < static_cast<int>(closes.size()); i++) {
        double line = m.update(closes[i]);
        expectSame(batch.line[i], line, i, "MACD line");
        expectSame(batch.signal[i], m.signal(), i, "MACD signal");
        expectSame(batch.histogram[i], m.histogram(), i, "MACD histogram");
    }
}

TEST(vwapAccumulates) {
    StreamingIndicators::Vwap v;
    v.update(10, 100);
    double res = v.update(20, 100);
    // (10*100 + 20*100) / 200 = 15.
    EXPECT_NEAR(res, 15.0, 1e-12);
    EXPECT_NEAR(v.value(), 15.0, 1e-12);
}

QTEST_MAIN()
