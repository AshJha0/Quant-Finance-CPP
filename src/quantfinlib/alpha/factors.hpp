// Port of com.quantfinlib.alpha.Factors: the standard alpha factor
// library -- nine signal generators (trend, mean-reversion, fundamental/
// defensive families). Each factory returns an AlphaFactor producing raw
// cross-sectional scores where higher = more attractive long. All
// computations read only bars <= index (no-look-ahead) and honor
// AlphaContext::isActive.
#pragma once

#include "quantfinlib/alpha/alpha_factor.hpp"

namespace quantfinlib {
namespace factors {

/** (SMA_fast - SMA_slow) / SMA_slow. */
AlphaFactor movingAverageCrossover(int fast, int slow);

/** (macdLine - signalLine) / close. */
AlphaFactor macd(int fast, int slow, int signal);

/** close[i-skip] / close[i-lookback] - 1. */
AlphaFactor momentum(int lookback, int skip);

/** Cutler's RSI, contrarian sign: (50 - RSI) / 50. */
AlphaFactor rsi(int period);

/** Bollinger mean reversion: -(close - SMA) / (k*sigma). */
AlphaFactor bollinger(int period, double stdDevs);

/** Plain mean reversion: -(close / SMA - 1). */
AlphaFactor meanReversion(int lookback);

/** Value composite: average of earnings yield and book yield. */
AlphaFactor value();

/** Quality composite: ROE - 0.1*debt/equity. */
AlphaFactor quality();

/** Low-volatility anomaly: -sigma(returns) over the lookback. */
AlphaFactor lowVolatility(int lookback);

}  // namespace factors
}  // namespace quantfinlib
