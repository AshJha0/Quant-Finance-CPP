# Quant-Finance-CPP

A C++26 quant finance and low-latency trading library, with no external dependencies. It began as a port of the Java [Quant-Finance-Library](https://github.com/AshJha0/Quant-Finance-Library) v1.17 and has since taken selected fixes from Java 4.0, plus its own work.

**What this is:** a verified research and engineering reference. **What it is not:** a production trading system. It is not venue-certified, has no TLS, and contains no evidence of trading edge.

## What is inside

| Area | Packages | Notes |
|---|---|---|
| Pricing and rates | `pricing`, `rates`, `credit`, `fx`, `volatility`, `commodities`, `markets` | Black-Scholes family, Bachelier, Heston, SABR, SVI, barriers, multi-curve OIS bootstrap, day counts, holiday calendars, ISDA CDS, GARCH MLE |
| Risk | `risk` | VaR, ES, FRTB ES fragment, stress, copulas, backtests |
| Research | `backtest`, `alpha`, `microstructure` | Next-open fills, as-of data, overfitting statistics, impact and queue models |
| Execution | `execution`, `crb` | Schedulers, SOR, queue fill simulator, central risk book calculator |
| Live lane | `trading`, `orderbook`, `marketdata` | Fail-closed gate, order lifecycle, kill sources, bus, rings, book |
| Connectivity | `fix`, `sbe`, `persist`, `data` | FIX 4.4 subset with venue bridge, ITCH subset, MoldUDP64, IEX, checkpoints |

Read [ARCHITECTURE](docs/ARCHITECTURE.md), the guided tour in [LEARN](docs/LEARN.md), and the [CHANGELOG](CHANGELOG.md). Version 2.0.0 was breaking; 2.1.0 requires a C++26 compiler.

## Evidence

Each claim has a way to check it.

- **Tests:** about 100 executables and 1,099 `TEST` cases, on GCC, Clang, ASan/UBSan and TSan in CI.
- **Correctness:** golden tests from QuantLib 1.44, scipy and mpmath, in `tests/golden_*`, independent of the Java code.
- **Real messages:** one day of IEX DEEP and DEEP+ replayed with no gaps or crossed books. See [REPLAY_VALIDATION](docs/REPLAY_VALIDATION.md). Correctness only, one day, one venue. Data provided for free by IEX; [terms](https://www.iex.io/products/market-data-connectivity/hist-terms).
- **Controls:** [CONTROLS](docs/CONTROLS.md) maps controls to MiFID II RTS 6 and SEC 15c3-5. It is not a compliance certification.

## Latency

Indicative only: C++26 build, median of three runs, WSL2, unpinned, Intel Core 5 120U, GCC 15. Run `ull_bench` to reproduce it on your machine. Tails here measure the host, not the code.

| Stage | p50 |
|---|---|
| Book add / cancel, 16k resting | 35 / 45 ns |
| Book add / cancel, 1M resting | 132 / 264 ns |
| Ring hand-off, busy-spin | 126 ns |
| Bus publish to listener, busy-spin | 148 ns |
| Single-thread code path, no I/O or core hop | 43 ns |

The timer floor of about 46 ns is not subtracted, and values near it are unresolved. None of these is wire to wire. For Java, see [the side-by-side run](docs/JAVA_CPP_COMPARISON.md), which does not support a specific speed-up figure.

## Known gaps

- No TLS, no OrderStatusRequest, ExecID history not persisted, `OrderThrottle` not wired, and nothing joins ITCH or MoldUDP64 to the bus.
- A standalone gate gets no automatic reference price, and a bad but finite tick can move the collar.
- Not done in maths: dated legs, Jacobian risk, SVI surface, GJR and EGARCH MLE.
- `LiquiditySeekingAlgo` and `LpRouter` are not wired to guarded routing. Portfolio backtests have no dividends or membership gating.
- `backtest` and `execution` include each other.

## Build and test

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The library builds as C++26. Compilers do not yet ship all of it: on GCC 15 and Clang 21, saturating arithmetic is available, while contracts, reflection, hazard pointers and RCU are not, so the code does not use those.

Linux with GCC or Clang is the main platform; MinGW is tested in CI. Benchmarks use GCC builtins and do not build on MSVC.

MIT licensed.
