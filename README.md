# Quant-Finance-CPP

C++20 port of [Quant-Finance-Library](https://github.com/AshJha0/Quant-Finance-Library)
(the Java reference implementation, v1.17.0) -- zero external dependencies,
standard library only, tests on a vendored micro-harness.

The port is algorithm-faithful: every class mirrors its Java original
method-for-method (camelCase names kept), the numerical algorithms are
transcribed digit-for-digit (Acklam normInv, Lanczos logGamma, Lentz
incomplete beta, Cholesky with the relative pivot rules), and the Java
test suite's hand-pinned values transfer at the same tolerances --
including a bit-exact `SplittableRandom` so seeded Monte Carlo replays
the Java streams. NaN-rejecting input gates throw `std::invalid_argument`;
solvers are bracket-checked and never return an endpoint.

## Phase 1 (current): the quant core

| Package | Contents |
|---|---|
| `util` | MathUtils: 27 numerical primitives |
| `data` | Bar (validates high >= low, as the Java record does) |
| `pricing` | Black-Scholes (all greeks, implied vol), Black-76, higher-order greeks, binomial tree, digitals, barriers, touches, vanna-volga, Margrabe/Kirk, quanto, variance swap, Asian (Kemna-Vorst + Turnbull-Wakeman), structured notes, autocallable, Heston (stable formulation), SABR, vol surface, forward curve, dividends, fair value, triangular arbitrage |
| `rates` | YieldCurve bootstrap, bond pricer (bracket-checked YTM), Nelson-Siegel, Svensson, swap pricer (annuity/par/DV01), Vasicek/CIR/Hull-White, key-rate durations, swaptions/caps |
| `credit` | Credit curve (hazard bootstrap), CDS pricer (incl. the sub-grid maturity gate), Z-spread, CVA |
| `commodities` | Futures curve: roll yield, implied carry, contango/backwardation |
| `markets` | Index construction (divisor continuity), private markets (IRR/TVPI/KS-PME/Geltner) |
| `risk` | Four VaR flavors + ES, component VaR (Euler), Ledoit-Wolf shrinkage, EVT, stress + reverse stress, FRTB ES, P&L attribution (KS), VaR backtests, PCA, Gaussian/t copulas, concentration, counterparty PFE, settlement risk |
| `volatility` | EWMA, GARCH/GJR/EGARCH, HAR-RV, model-free vol index, range estimators (Parkinson/GK/RS/Yang-Zhang), AIC/BIC, vol decomposition |
| `indicators` | Batch + streaming indicator set (SMA/EMA/RSI/MACD/Bollinger/ATR/VWAP...) |
| `backtest` | Trade analytics, performance/drawdown analytics, benchmark comparison; `validation/` purged K-fold, CSCV overfit probability, block bootstrap, deflated Sharpe, Monte Carlo trade reshuffle; `portfolio/` position sizing, optimizers (MV/risk-parity/Black-Litterman/constrained) |

86 headers - 270 tests across 28 executables, all green.

Phase 2 (planned): the execution/microstructure layer -- order book,
lock-free rings, market-making loop -- with re-runnable nanosecond
benchmarks (this is where C++ is expected to beat the Java baselines of
204 ns tick-to-strategy / 504 ns tick-to-order). Phase 3: the teaching
docs adapted to C++.

## Build and test

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Any C++20 compiler works; developed against GCC 16 (MinGW-w64) and
CMake 4.4. New sources are picked up by glob -- add files, not build
script edits.
