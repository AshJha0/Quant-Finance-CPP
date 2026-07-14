#include "quantfinlib/alpha/factors.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/alpha/alpha_context.hpp"

namespace quantfinlib {
namespace factors {

namespace {

double kNaN = std::numeric_limits<double>::quiet_NaN();

double sma(const BarSeries& s, int index, int period) {
    double sum = 0;
    for (int j = index - period + 1; j <= index; j++) {
        sum += s.close(j);
    }
    return sum / period;
}

void requireWindow(bool ok, const std::string& message) {
    if (!ok) {
        throw std::invalid_argument(message);
    }
}

using ScoreFn = std::function<double(const BarSeries&, int)>;

/** Wraps per-symbol window math into the cross-sectional contract. */
AlphaFactor named(std::string name, int minBars, ScoreFn perSymbol) {
    return AlphaFactor(std::move(name), [minBars, perSymbol](const AlphaContext& ctx, int index) {
        std::vector<double> out(static_cast<std::size_t>(ctx.symbolCount()));
        for (int i = 0; i < static_cast<int>(out.size()); i++) {
            if (index < minBars) {
                out[static_cast<std::size_t>(i)] = kNaN;
            } else if (!ctx.isActive(i, index)) {
                out[static_cast<std::size_t>(i)] = kNaN;
            } else {
                out[static_cast<std::size_t>(i)] = perSymbol(ctx.series(i), index);
            }
        }
        return out;
    });
}

/** Wraps a fundamentals read; NaN for symbols without a snapshot. */
AlphaFactor fundamental(std::string name, std::function<double(const Fundamentals&)> f) {
    return AlphaFactor(std::move(name), [f](const AlphaContext& ctx, int index) {
        std::vector<double> out(static_cast<std::size_t>(ctx.symbolCount()));
        for (int i = 0; i < static_cast<int>(out.size()); i++) {
            const Fundamentals* fu = ctx.fundamentals(i);
            out[static_cast<std::size_t>(i)] =
                    (fu == nullptr || !ctx.isActive(i, index)) ? kNaN : f(*fu);
        }
        return out;
    });
}

}  // namespace

AlphaFactor movingAverageCrossover(int fast, int slow) {
    requireWindow(fast > 0 && slow > fast, "need slow > fast > 0");
    return named("MA_CROSS(" + std::to_string(fast) + "," + std::to_string(slow) + ")", slow,
                 [fast, slow](const BarSeries& s, int i) {
                     double f = sma(s, i, fast);
                     double sl = sma(s, i, slow);
                     return (f - sl) / sl;
                 });
}

AlphaFactor macd(int fast, int slow, int signal) {
    requireWindow(fast > 0 && slow > fast && signal > 0, "need slow > fast > 0, signal > 0");
    int warmup = slow + signal;
    return named(
            "MACD(" + std::to_string(fast) + "," + std::to_string(slow) + "," + std::to_string(signal)
                    + ")",
            warmup, [fast, slow, signal](const BarSeries& s, int i) {
                int start = std::max(0, i - 4 * slow + 1);
                double kFast = 2.0 / (fast + 1);
                double kSlow = 2.0 / (slow + 1);
                double kSignal = 2.0 / (signal + 1);
                double emaFast = s.close(start);
                double emaSlow = s.close(start);
                double signalLine = kNaN;
                double macdLine = 0;
                for (int j = start; j <= i; j++) {
                    if (j > start) {
                        double close = s.close(j);
                        emaFast += kFast * (close - emaFast);
                        emaSlow += kSlow * (close - emaSlow);
                    }
                    macdLine = emaFast - emaSlow;
                    signalLine = std::isnan(signalLine) ? macdLine
                                                         : signalLine + kSignal * (macdLine - signalLine);
                }
                return (macdLine - signalLine) / s.close(i);
            });
}

AlphaFactor momentum(int lookback, int skip) {
    requireWindow(lookback > 0 && skip >= 0 && skip < lookback, "need lookback > skip >= 0");
    return named("MOMENTUM(" + std::to_string(lookback) + "-" + std::to_string(skip) + ")", lookback,
                 [lookback, skip](const BarSeries& s, int i) {
                     return s.close(i - skip) / s.close(i - lookback) - 1;
                 });
}

AlphaFactor rsi(int period) {
    requireWindow(period > 0, "need period > 0");
    return named("RSI_CUTLER_REV(" + std::to_string(period) + ")", period + 1,
                 [period](const BarSeries& s, int i) {
                     double gain = 0;
                     double loss = 0;
                     for (int j = i - period + 1; j <= i; j++) {
                         double change = s.close(j) - s.close(j - 1);
                         if (change > 0) {
                             gain += change;
                         } else {
                             loss -= change;
                         }
                     }
                     if (gain + loss == 0) {
                         return 0.0;
                     }
                     double r = 100 * gain / (gain + loss);
                     return (50 - r) / 50;
                 });
}

AlphaFactor bollinger(int period, double stdDevs) {
    requireWindow(period > 1 && stdDevs > 0, "need period > 1 and stdDevs > 0");
    return named("BOLL_REV(" + std::to_string(period) + "," + std::to_string(stdDevs) + ")", period,
                 [period, stdDevs](const BarSeries& s, int i) {
                     double mean = sma(s, i, period);
                     double var = 0;
                     for (int j = i - period + 1; j <= i; j++) {
                         double d = s.close(j) - mean;
                         var += d * d;
                     }
                     double sd = std::sqrt(var / period);
                     if (sd == 0) {
                         return 0.0;
                     }
                     return -(s.close(i) - mean) / (stdDevs * sd);
                 });
}

AlphaFactor meanReversion(int lookback) {
    requireWindow(lookback > 0, "need lookback > 0");
    return named("MEAN_REV(" + std::to_string(lookback) + ")", lookback,
                 [lookback](const BarSeries& s, int i) { return -(s.close(i) / sma(s, i, lookback) - 1); });
}

AlphaFactor value() {
    return fundamental("VALUE", [](const Fundamentals& f) {
        double sum = 0;
        int n = 0;
        if (!std::isnan(f.peRatio) && f.peRatio != 0) {
            sum += 1 / f.peRatio;
            n++;
        }
        if (!std::isnan(f.pbRatio) && f.pbRatio != 0) {
            sum += 1 / f.pbRatio;
            n++;
        }
        return n == 0 ? kNaN : sum / n;
    });
}

AlphaFactor quality() {
    return fundamental("QUALITY", [](const Fundamentals& f) {
        double roe = f.roe;
        double leverage = std::isnan(f.debtToEquity) ? 0 : f.debtToEquity;
        return std::isnan(roe) ? kNaN : roe - 0.1 * leverage;
    });
}

AlphaFactor lowVolatility(int lookback) {
    requireWindow(lookback > 1, "need lookback > 1");
    return named("LOW_VOL(" + std::to_string(lookback) + ")", lookback + 1,
                 [lookback](const BarSeries& s, int i) {
                     double mean = 0;
                     for (int j = i - lookback + 1; j <= i; j++) {
                         mean += s.close(j) / s.close(j - 1) - 1;
                     }
                     mean /= lookback;
                     double var = 0;
                     for (int j = i - lookback + 1; j <= i; j++) {
                         double d = (s.close(j) / s.close(j - 1) - 1) - mean;
                         var += d * d;
                     }
                     return -std::sqrt(var / lookback);
                 });
}

}  // namespace factors
}  // namespace quantfinlib
