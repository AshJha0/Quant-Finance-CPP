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

## What's ported (Phases 1-3)

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
| `backtest` | Full strategy engine (Backtester, 5 strategies, cost models, execution models with the cash-conservation contract, walk-forward with warm folds, grid search) + trade/performance/drawdown analytics; `validation/` purged K-fold, CSCV overfit probability, block bootstrap, deflated Sharpe, Monte Carlo trade reshuffle, bit-exact Java SplittableRandom; `portfolio/` position sizing, optimizers, portfolio backtester |
| `orderbook` | HftOrderBook (dense ladder, bitmaps, pooled nodes, IOC/FOK/post-only) proven equivalent to the reference book under random operation streams |
| `trading` | HftRiskGate (atomics, reduce-vs-flip), order ring buffer |
| `fx` | Currency-pair conventions + calendars, swap points, FX swaps/NDFs, delta-quoted vol surface (premium-adjusted), tier book, LP scorecard/router |
| `crb` | Central risk book: factor-space netting, skewed quoting, internalization, L1 hedge optimizer, auto-hedger, router, P&L ledger |
| `alpha` | Research pipeline: context/factors/evaluator/validation/backtester/construction/report, Fama-MacBeth, calendar anomalies |
| `microstructure` | Almgren-Chriss, TCA, impact models, seasonality curves (per-bucket seeding), queue/fill models, Lee-Ready, VPIN, Hawkes, EWMA covariance, Avellaneda-Stoikov, ensemble + online learner |

237 headers - 730 tests across 81 executables, all green.

Phase 5 (done): the live trading lane -- `HftMarketDataBus`
(copy-on-write listeners, SPSC rings), `SymbolRegistry`, async tick
capture with honest drop counting, `HftOrderGateway`,
`ShardedTradingEngine`, `GlobalRiskAggregator` (hysteretic breaker),
`HftQuoter`/`AutoHedger`, `PaperTradingGateway`, `SignalEngine`; the
full `execution/` package (TWAP/VWAP/POV/IS schedulers, SOR + adaptive
routing, dark-pool MEQ dry-run, BenchmarkExecutor, PortfolioExecutor);
and `persist/Checkpoint` byte-identical to the Java/Python format --
a checkpoint written by Python restores in C++ (pinned by test).
The deterministic 1M-op soak asserts exact conservation
(processed == submitted == delivered); tick-to-strategy through the
real bus measures p50 228 ns (Java baseline 204 ns).

**Measured nanosecond benchmarks** (`benchmarks/ull_bench.cpp`, medians
on an unpinned Windows box -- the binary prints its own caveats):
gate check 2.7 ns/op (Java ~3 ns), book mixed add/cancel 49.5 ns/op
(Java ~204 ns), ring publish-to-consume p50 98 ns (Java 204 ns),
tick-to-order composite p50 47 ns (Java p50 504 ns). The zero-allocation
claim is enforced by a counting `operator new` hook over a million
steady-state operations.

**Cross-port verified**: an identical probe battery runs against Java,
C++, and Python (138 labels) and diffs to zero mismatches -- the harness
lives in the Python repo at `tools/crossport/`.

Phase 4 (done): `fix/` -- the full FIX 4.4 engine (codec, incremental
framer with the zombie-session guard, garbage-free encoder/view,
initiator/acceptor session over sockets with resend recovery and
ResetSeqNumFlag, durable session store); `marketdata/` + `sbe/` --
ITCH codec, L3 book builder with exact queue position, NBBO, byte-exact
SBE flyweights; `data/` QFLT tick files.

**New to the codebase?** Start with [docs/LEARN.md](docs/LEARN.md) --
a guided tour with compiled-and-run snippets, five worked experiments
(including the benchmark run), and pointers into the Java repo's
teaching material (1000 exercises, 300 recipes, 100 diagrams -- the
concepts transfer 1:1, only syntax differs).

Remaining (deliberate): WebSocket feeds, the HTML dashboard, CLI and
report generators -- JVM-app conveniences, not library logic.

## Build and test

```bash
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

Any C++20 compiler works; developed against GCC 16 (MinGW-w64) and
CMake 4.4. New sources are picked up by glob -- add files, not build
script edits.
