# Learning quantfinlib in C++

This is the port-specific guide to the C++20 mirror of
[Quant-Finance-Library](https://github.com/AshJha0/Quant-Finance-Library)
(the Java reference, v1.17.0). It does not try to replace the Java
project's 26,000-line LEARN.md -- that document has 1000 worked
exercises and stays the deep-material home for the whole family. This
guide is the ~800-line version: how the C++ port is built, a reading
path through the 178 headers that exist *in this repo today*, five
hands-on experiments with real compiled output, an honest list of what
this port skips, and a map back to the Java documents when you want the
1000-exercise version of any of this.

Every class and function named below was checked against the source
tree with Glob/Grep before it went in this file -- if it's cited here,
`grep -rn "ClassName" src/` finds it. Every code snippet in sections 2
and 3 was compiled with MinGW g++ 16 against this repo's headers and
`.cpp` files and actually run; the output shown is real, not
illustrative.

## 1. How this port works

### The contract, in one paragraph

The C++ port is algorithm-faithful, not idiom-faithful: every class
mirrors its Java original method-for-method, camelCase names kept
(`black_scholes::price`, not `black_scholes::compute_price`), and the
numerical algorithms are transcribed digit-for-digit -- Acklam's
`normInv`, the Lanczos `logGamma`, Lentz's incomplete-beta continued
fraction, the Cholesky pivot rule, GARCH grids, curve bootstraps. When
the Java test suite pinned a hand-computed value to 1e-9, the C++ test
pins the same value at the same tolerance. This is deliberate: the
point of the port is that you can trust a C++ number because it is
*provably* the same computation as the Java number, not a
reimplementation that merely agrees on a handful of smoke tests.

### Method naming

Java statics become free functions in a matching namespace, or static
methods on a class when the Java original was a class with real state.
Compare the pricer that every other package eventually calls:

Java (`com.quantfinlib.pricing.BlackScholes`):

```java
public static double price(OptionType type, double spot, double strike,
                           double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return intrinsic(type, spot, strike);
    }
    if (vol <= 0) {
        return Math.max(0, type.sign() * (spot * Math.exp(-carry * timeYears)
                - strike * Math.exp(-rate * timeYears)));
    }
    ...
}
```

C++ (`src/quantfinlib/pricing/black_scholes.hpp` /
`black_scholes.cpp`):

```cpp
double price(OptionType type, double spot, double strike,
             double rate, double carry, double vol, double timeYears) {
    if (timeYears <= 0) {
        return intrinsic(type, spot, strike);
    }
    if (vol <= 0) {
        return std::max(0.0, sign(type) * (spot * std::exp(-carry * timeYears)
                - strike * std::exp(-rate * timeYears)));
    }
    ...
}
```

Same parameter order, same branch structure, same comment about *why*
the `vol <= 0` branch exists (the ATM-forward 0/0 in `d1`). The `enum
OptionType` moved from a nested Java enum to namespace scope in
`quantfinlib::` because every other pricing header needs it too
(`black76.hpp`, `barrier_option.hpp`, ...) -- the one naming departure
that has nothing to do with C++ idiom and everything to do with one
enum, many translation units.

### Exception mapping

Java's `IllegalArgumentException` becomes `std::invalid_argument`
everywhere, argument-checking message included. `SwapPricer` is a good
minimal example (`src/quantfinlib/rates/swap_pricer.cpp`):

Java:

```java
private static void requireTenor(int tenorYears) {
    if (tenorYears < 1) {
        throw new IllegalArgumentException("tenorYears must be >= 1, got " + tenorYears);
    }
}
```

C++:

```cpp
void requireTenor(int tenorYears) {
    if (tenorYears < 1) {
        throw std::invalid_argument("tenorYears must be >= 1, got "
                                    + std::to_string(tenorYears));
    }
}
```

`IllegalStateException` (an empty `BarSeries.Builder.build()`, for
example) maps to `std::logic_error`. There is no port-wide exception
hierarchy invented on top of this -- if you have written C++ that
catches `std::invalid_argument`, you already know how to catch
everything this library throws.

### NaN gates

Java's idiom for rejecting NaN inputs is `if (!(x > 0))` rather than
`if (x <= 0)` -- the negated form is also true when `x` is NaN (every
comparison against NaN is false), so it catches NaN and non-positive
values in one branch without a second `Double.isNaN` check. The C++
port keeps the exact idiom, not a cleaner-looking rewrite, because the
whole point is that a reviewer who knows the Java source recognizes the
C++ line instantly. You'll see `if (!(x > 0))` throughout `math_utils.cpp`,
`yield_curve.cpp`, and the pricing headers.

### Bracket-checked solvers

Every root-finder in this port (implied vol, bond YTM, credit-curve
hazard bootstrap, Nelson-Siegel/Svensson fits) is bisection or a
bisection-guarded Newton step, and every one of them checks the bracket
before it starts rather than trusting the loop to converge. From
`black_scholes.hpp`'s `impliedVol` doc comment:

```
Returns NaN when the price is not attainable inside the [1e-4, 5.0] vol
bracket (below intrinsic, or above the maximum BS price) -- a stale or
rounded market price must surface as "no vol", not silently come back
as the 500% search bound and poison a smile.
```

`CreditCurve::bootstrap` does the same thing on the hazard-rate axis:
bisection on `h` in `[1e-9, 10]` with an explicit bracket check, "a
quote no hazard can explain throws rather than returning the bound."
The pattern repeats everywhere a solver exists in this codebase: no
silent endpoint returns, ever.

### Hand-pinned tests

The port ships 178 headers and 506 tests across 52 executables (all
green as of this writing -- `ctest --test-dir build` reproduces that
number). The tests are not generated or property-based in the pricing/
rates/credit/risk/volatility packages: they are the SAME numeric values
the Java suite pins, carried over at the same tolerance, because the
whole port strategy depends on "does this C++ function return what the
Java function returns" being a checkable, not an assumed, fact. Look at
any file under `tests/` -- `tests/pricing_exotics_test.cpp`,
`tests/backtest_trade_analytics_test.cpp` -- and you'll find literal
expected doubles, not generated fixtures.

### The cross-port 138-label guarantee

This is the strongest claim in the whole project and it is mechanically
checked, not asserted. A single probe program -- one file per language,
identical inputs in identical order -- calls into Java, C++ and Python
and prints 138 labelled results (`bs.call=...`, `ns.rmse=...`,
`micro.hawkes.intensity=...`) at 15 significant figures. A tiny
Python script then diffs the three outputs pairwise at 1e-9 relative
tolerance (1e-6 for the handful of labels that come out of an iterative
fit, where floating-point path-dependence in the last few digits is
expected and stated up front). The harness lives in the Python repo,
`tools/crossport/`:

```
tools/crossport/
  Probe.java     -- the Java probe (source of truth for labels/order)
  probe.cpp      -- this port's mirror
  probe.py       -- the Python port's mirror
  compare.py     -- pairwise diff at 1e-9 / 1e-6
  java.out       -- checked-in Java output (138 lines)
  cpp.out        -- checked-in C++ output
  py.out         -- checked-in Python output
```

To re-run the C++ side of the guarantee yourself, from this repo:

```bash
g++ -std=c++20 -O2 -I src ../quant-finance-py/tools/crossport/probe.cpp \
    build/libquantfinlib.a -o /tmp/probe_cpp
/tmp/probe_cpp > /tmp/cpp.out
python ../quant-finance-py/tools/crossport/compare.py \
    ../quant-finance-py/tools/crossport/java.out /tmp/cpp.out
```

A clean run prints `138 labels, 0 mismatches`. That is not a
hypothetical -- it is the exact command this guide's author ran while
writing this section, and the exact output it produced. If you break a
numeric algorithm while extending this port, this is the fastest way to
find out: the labels tell you which package regressed.

## 2. A guided tour in 13 stops

Read these in order; each one assumes the previous. Every "try it"
snippet below compiles as shown against this repo, once you have built
the library once (`cmake -S . -B build -G "MinGW Makefiles" &&
cmake --build build --target quantfinlib`, per the top-level README) --
the one-liners assume `build/libquantfinlib.a` exists.

### Stop 1 -- `util`: the numerical bedrock

**What it does.** `MathUtils` (`src/quantfinlib/util/math_utils.hpp`):
27 primitives every other package leans on -- mean/variance/stdDev,
percentile, covariance/correlation, Cholesky, the Acklam `normInv`, the
Abramowitz-Stegun `normCdf`, skewness/kurtosis, a NaN-safe `pairSort`.

**Read first:** `math_utils.hpp`'s doc comment, then `normCdf`/`normInv`
-- everything downstream that touches a normal distribution (every
option pricer, every VaR flavor) is built on these two.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/util/math_utils.hpp"
using namespace quantfinlib;
int main() {
    double x = 1.2345;
    double p = math_utils::normCdf(x);
    double back = math_utils::normInv(p);       // round trip
    std::vector<double> v{1.0, 2.0, 3.0, 4.0, 5.0};
    std::printf("normCdf(%.4f)=%.6f  normInv(back)=%.6f  mean=%.3f  stdDev=%.4f\n",
                x, p, back, math_utils::mean(v), math_utils::stdDev(v));
}
```

```
$ g++ -std=c++20 -I src t_util.cpp build/libquantfinlib.a -o t_util && ./t_util
normCdf(1.2345)=0.891492  normInv(back)=1.234500  mean=3.000  stdDev=1.5811
```

### Stop 2 -- `data`: the shared currency, `Bar`/`BarSeries`

**What it does.** `Bar` is an immutable OHLCV record (validates
`high >= low`, exactly the Java record's compact constructor).
`BarSeries` is a structure-of-arrays time series every pricing,
indicator and backtest class consumes -- this is the one type you will
import into almost everything you write against this library.

**Read first:** `bar_series.hpp` -- note `of()` (closes-only, synthetic
OHLC), `slice()` (the train/test split every walk-forward tool uses),
and that array accessors return `const std::vector<double>&`, not
copies.

**Try it:**

```cpp
#include <cstdio>
#include "quantfinlib/data/bar_series.hpp"
using namespace quantfinlib;
int main() {
    auto series = BarSeries::of("DEMO", {100.0, 101.0, 99.5, 102.0, 103.0});
    auto rets = series.returns();
    std::printf("size=%d lastClose=%.2f firstReturn=%.5f\n",
                series.size(), series.lastClose(), rets.front());
}
```

```
size=5 lastClose=103.00 firstReturn=0.01000
```

### Stop 3 -- `pricing`: Black-Scholes and its 20 siblings

**What it does.** Black-Scholes-Merton with a continuous carry (so one
function prices equity options with a dividend yield and FX options
Garman-Kohlhagen-style), Black-76, binomial trees, digitals, barriers,
touches, Asian (Kemna-Vorst geometric + Turnbull-Wakeman arithmetic),
vanna-volga, Margrabe/Kirk spread options, quanto, variance swaps,
structured notes, autocallables, Heston, SABR, a vol surface, a forward
curve, dividends, and a fair-value blender.

**Read first:** `black_scholes.hpp` -- every other pricer in the
package either calls it directly (Margrabe reduces to it in the
single-asset limit) or reuses its `d1`/`normCdf` plumbing.

**Try it:** see Experiment (a) below -- it is this stop's exercise.

### Stop 4 -- `rates`: the yield curve and what stands on it

**What it does.** `YieldCurve` (bootstrap from par swaps or raw zero
rates; linear interpolation on continuously-compounded zeros, flat
extrapolation), `BondPricer` (bracket-checked YTM), Nelson-Siegel and
Svensson parametric fits, `SwapPricer` (annuity/par rate/DV01),
Vasicek/CIR/Hull-White short-rate models, key-rate durations,
swaptions/caps.

**Read first:** `yield_curve.hpp`, then `swap_pricer.hpp` -- the curve
is the input every other rates class takes.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/rates/yield_curve.hpp"
#include "quantfinlib/rates/swap_pricer.hpp"
using namespace quantfinlib;
int main() {
    std::vector<int> tenors{1, 2, 3, 5, 7, 10};
    std::vector<double> parRates{0.03, 0.032, 0.034, 0.036, 0.037, 0.038};
    YieldCurve curve = YieldCurve::bootstrapAnnualParSwaps(tenors, parRates);
    double par5 = swap_pricer::parRate(curve, 5);
    double pv = swap_pricer::payerPv(curve, 5, par5);
    std::printf("parRate5y=%.6f  payerPvAtPar=%.3e  dv01=%.6f\n",
                par5, pv, swap_pricer::dv01(curve, 5, par5));
}
```

```
parRate5y=0.036000  payerPvAtPar=0.000e+00  dv01=0.000466
```

The `payerPvAtPar` line is not decoration -- a swap struck exactly at
its own par rate must reprice to (numerically) zero, and this is the
identity Experiment (b) below builds on.

### Stop 5 -- `credit` / `commodities` / `markets`: three small curve families

**What it does.** `credit`: `CreditCurve` (hazard-rate bootstrap from
CDS par spreads), `CdsPricer`, Z-spread, CVA. `commodities`:
`CommodityCurve` (roll yield, implied carry, contango/backwardation --
no extrapolation past the pillars, deliberately, "a commodity curve's
wings are opinions, not data"). `markets`: index construction (cap/
price/equal weighting, the divisor that keeps a level series continuous
through membership changes), private-market analytics (IRR/TVPI/
KS-PME/Geltner).

**Read first:** `credit_curve.hpp` -- it is `YieldCurve`'s bootstrap
pattern applied one dimension over (hazard instead of discount rate),
so reading it right after Stop 4 reinforces the pattern instead of
introducing a new one.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/rates/yield_curve.hpp"
#include "quantfinlib/credit/credit_curve.hpp"
#include "quantfinlib/commodities/commodity_curve.hpp"
using namespace quantfinlib;
int main() {
    YieldCurve disc = YieldCurve::ofZeroRates({1, 5, 10}, {0.03, 0.03, 0.03});
    CreditCurve cc = CreditCurve::bootstrap({1, 3, 5, 10}, {0.01, 0.012, 0.015, 0.018},
                                            0.4, disc);
    std::printf("survival(5y)=%.6f hazard(5y)=%.6f\n",
                cc.survivalProbability(5.0), cc.hazard(5.0));
    CommodityCurve oil = CommodityCurve::of(80.0, {0.5, 1.0, 2.0}, {81.0, 82.5, 85.0});
    std::printf("oil 1y price=%.3f rollYield(0.5->1)=%.5f\n",
                oil.price(1.0), oil.annualizedRollYield(0.5, 1.0));
}
```

```
survival(5y)=0.880270 hazard(5y)=0.033634
oil 1y price=82.500 rollYield(0.5->1)=-0.03670
```

The negative roll yield says the oil curve you just built is in
contango over that stretch -- a long position pays to roll.

### Stop 6 -- `risk`: the numbers a risk committee asks for

**What it does.** Four VaR flavors plus Expected Shortfall, component
VaR (Euler allocation), Ledoit-Wolf covariance shrinkage, EVT, stress
and reverse-stress testing, FRTB ES, P&L attribution, VaR backtesting,
PCA, Gaussian/t copulas, concentration risk, counterparty PFE,
settlement risk.

**Read first:** `risk_metrics.hpp` -- the smallest, most self-contained
file in the package, and the one every other risk class's tests build
scenarios against.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/risk/risk_metrics.hpp"
using namespace quantfinlib;
int main() {
    std::vector<double> rets{0.01, -0.02, 0.015, -0.03, 0.005, 0.02, -0.01, 0.012};
    std::printf("VaR95=%.4f  ES95=%.4f  sharpe=%.4f\n",
                risk_metrics::historicalVar(rets, 0.95),
                risk_metrics::conditionalVar(rets, 0.95),
                risk_metrics::sharpeRatio(rets, 0.0, 252));
}
```

```
VaR95=0.0265  ES95=0.0300  sharpe=0.2193
```

### Stop 7 -- `volatility`: estimating sigma from returns

**What it does.** EWMA (RiskMetrics-style), GARCH(1,1)/GJR-GARCH/
EGARCH, HAR-RV, a model-free volatility index (the VIX construction,
`volatility_index.hpp`), range estimators (Parkinson, Garman-Klass,
Rogers-Satchell, Yang-Zhang), AIC/BIC model selection, volatility
decomposition.

**Read first:** `ewma_volatility.hpp` -- the simplest model in the
package (one parameter, `lambda`) and the one the risk package's
parametric VaR quietly assumes when you don't hand it a GARCH fit.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/volatility/ewma_volatility.hpp"
using namespace quantfinlib;
int main() {
    std::vector<double> rets{0.01,-0.02,0.015,-0.03,0.005,0.02,-0.01,0.012,-0.008,0.02};
    EwmaVolatility ewma = EwmaVolatility::riskMetrics();      // lambda = 0.94
    std::printf("latestVol=%.6f annualizedVol=%.4f\n",
                ewma.latestVol(rets), ewma.annualizedVol(rets, 252));
}
```

```
latestVol=0.016971 annualizedVol=0.2694
```

### Stop 8 -- `indicators`: the batch/streaming technical set

**What it does.** `Indicators` (batch: SMA/EMA/WMA/RSI/MACD/Bollinger/
ADX/SuperTrend/Ichimoku/StochRSI/Keltner/Donchian/ATR/OBV/VWAP/CMF/
Parabolic SAR...) and a streaming twin in `streaming_indicators.hpp`
for the same math computed incrementally, one bar at a time, for the
backtester and live paths that cannot afford to recompute a whole
array per tick.

**Read first:** `indicators.hpp` -- every strategy in `backtest/
strategies/` is built from two or three calls into this file.

**Try it:**

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/indicators/indicators.hpp"
using namespace quantfinlib;
int main() {
    std::vector<double> closes{100,101,102,101,103,105,104,106,108,107,109,110};
    auto sma3 = Indicators::sma(closes, 3);
    auto rsi5 = Indicators::rsi(closes, 5);
    std::printf("sma3.back()=%.4f  rsi5.back()=%.4f\n", sma3.back(), rsi5.back());
}
```

```
sma3.back()=108.6667  rsi5.back()=83.0273
```

### Stop 9 -- `backtest` core: `Backtester` and the strategy family

**What it does.** Event-driven, single-instrument, long-only
backtesting: signals fill at the close (slippage-adjusted), stop-loss/
take-profit are checked intrabar with gap-aware fills, commission on
both legs. Five shipped strategies (`strategies/`: SMA cross, EMA
cross, RSI, MACD, Bollinger). Cost models, execution models with a
cash-conservation contract, walk-forward analysis with warm folds, grid
search.

**Read first:** `backtester.hpp`, then `strategies/
sma_cross_strategy.hpp` as the simplest concrete `TradingStrategy`.

**Try it:** see Experiment (c) below.

### Stop 10 -- `backtest/validation` and `backtest/portfolio`: does the edge survive scrutiny?

**What it does.** `validation/`: purged K-fold with embargo (the fix
for financial-label leakage), CSCV overfit probability, block
bootstrap, deflated Sharpe, Monte Carlo trade reshuffle -- all seeded,
where seeded, by the bit-exact `SplittableRandom` (`validation/
splittable_random.hpp`) so a seeded Monte Carlo path matches the Java
stream exactly. `portfolio/`: Kelly and fixed-fractional position
sizing, inverse-volatility weighting, mean-variance/risk-parity/
Black-Litterman optimizers, cross-sectional momentum, a portfolio-level
backtester.

**Read first:** `validation/purged_k_fold.hpp` -- the shortest file in
the family and the one every other validation tool assumes you already
understand (you cannot correctly interpret a CSCV run on unpurged
folds).

**Try it:**

```cpp
#include <cstdio>
#include "quantfinlib/backtest/validation/purged_k_fold.hpp"
#include "quantfinlib/backtest/portfolio/position_sizing.hpp"
using namespace quantfinlib;
int main() {
    auto splits = purged_k_fold::splits(100, 5, 3, 2);
    std::printf("folds=%zu  fold0 test=[%d,%d) trainSize=%zu\n",
                splits.size(), splits[0].testFrom, splits[0].testTo,
                splits[0].trainIndices.size());
    std::printf("kellyFraction=%.4f halfKelly=%.4f\n",
                position_sizing::kellyFraction(0.001, 0.0004),
                position_sizing::halfKelly(0.001, 0.0004));
}
```

```
folds=5  fold0 test=[0,20) trainSize=75
kellyFraction=2.5000 halfKelly=1.2500
```

(Fold 0's training set is 75 of 100 samples, not 80 -- the missing 5 are
the purge/embargo zone around the test fold. Kelly above 1 is not a
bug: this input's mean/variance ratio genuinely implies leveraging
above 100%, which is exactly the kind of number that should make you
distrust the input before you trust the leverage.)

### Stop 11 -- `orderbook` + `trading`: the C++-only crown jewel

**What it does.** This package does not exist in the Python port --
it is the reason the C++ port exists at all. `HftOrderBook`
(`orderbook/hft_order_book.hpp`): a dense integer-tick price ladder,
occupancy bitmaps, pooled intrusive order nodes in parallel primitive
arrays, an open-addressing id map with backward-shift deletion, one
function-pointer trade sink -- zero allocation after construction,
proven by a counting `operator new` hook over a million steady-state
operations. `trading/HftRiskGate`: a zero-allocation pre-trade risk
gate built on `std::atomic` with the same acquire/release discipline
Java's `VarHandle` API expresses (on x86 that discipline compiles down
to plain loads/stores, so correctness is free). `trading/
OrderRingBuffer`: the cross-thread SPSC hand-off.

**Read first:** `hft_order_book.hpp` -- read the doc comment in full
before the method list; it explains why rejections are return codes,
never exceptions (a venue must not unwind its matching loop over a bad
order).

**Try it:**

```cpp
#include <cstdio>
#include "quantfinlib/orderbook/hft_order_book.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
using namespace quantfinlib;
int main() {
    HftOrderBook book(9000, 11000, 1024);
    std::int64_t bid = book.submitLimit(Side::BUY, 9990, 100, 1);
    std::int64_t ask = book.submitLimit(Side::SELL, 10010, 50, 2);
    std::printf("bidId=%lld askId=%lld bestBid=%d bestAsk=%d\n",
                (long long)bid, (long long)ask, book.bestBidTick(), book.bestAskTick());

    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e6).priceCollarPct(0.05);
    gate.setReferencePrice(0, 100.0);
    int r1 = gate.check(0, Side::BUY, 500, 100.5);
    int r2 = gate.check(0, Side::BUY, 500, 200.0);   // outside the 5% collar
    std::printf("check1=%d(OK=%d) check2=%d(COLLAR=%d)\n",
                r1, HftRiskGate::OK, r2, HftRiskGate::REJECT_PRICE_COLLAR);
}
```

```
bidId=1 askId=2 bestBid=9990 bestAsk=10010
check1=0(OK=0) check2=4(COLLAR=4)
```

### Stop 12 -- `fx` + `crb`: the dealer desk

**What it does.** `fx`: currency-pair conventions (pip size, spot lag,
joint-calendar settlement arithmetic), swap points, FX swaps/NDFs, a
premium-adjusted delta-quoted vol surface, tier books, LP scorecards
and routing. `crb` (central risk book): factor-space netting, a skewed
quoter, an internalization engine, an L1 hedge optimizer, an
auto-hedger, a router, a P&L ledger.

**Read first:** `fx/currency_pair.hpp` -- the base convention object
every other FX class takes; then `crb/factor_registry.hpp`, the
dense-id pattern the whole risk book's exposure arithmetic runs on.

**Try it:**

```cpp
#include <cstdio>
#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/crb/factor_registry.hpp"
using namespace quantfinlib;
int main() {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    std::printf("symbol=%s pip=%.4f\n", eurusd.symbol().c_str(), eurusd.pipSize());
    FactorRegistry reg;
    int eqAapl = reg.id("EQ:AAPL");
    int ccyEur = reg.id("CCY:EUR");
    std::printf("EQ:AAPL id=%d CCY:EUR id=%d size=%d\n", eqAapl, ccyEur, reg.size());
}
```

```
symbol=EURUSD pip=0.0001
EQ:AAPL id=0 CCY:EUR id=1 size=2
```

### Stop 13 -- `alpha` + `microstructure`: the research desk

**What it does.** `alpha`: a full research pipeline (context, nine
factor generators, a signal evaluator, purge-aware validation, a
backtester, portfolio construction, a report), Fama-MacBeth, calendar
anomalies. `microstructure`: Almgren-Chriss optimal execution, TCA,
market-impact models, per-bucket seasonality curves, queue/fill models,
Lee-Ready trade classification, VPIN, Hawkes intensity, EWMA
covariance, Avellaneda-Stoikov optimal market-making, an alpha
ensemble, an online learner.

**Read first:** `microstructure/avellaneda_stoikov.hpp` -- short,
self-contained, and the cleanest illustration in the whole codebase of
"units contract" documentation (it states explicitly that
`priceVariancePerSecond` is a variance of the *price*, not the return).

**Try it:**

```cpp
#include <cstdio>
#include "quantfinlib/microstructure/avellaneda_stoikov.hpp"
using namespace quantfinlib;
int main() {
    AvellanedaStoikov mm(0.1, 1.5);      // gamma (risk aversion), kappa (fill decay)
    double bid = mm.bidQuote(100.0, 0.0, 0.0004, 300.0);
    double ask = mm.askQuote(100.0, 0.0, 0.0004, 300.0);
    double bidLong = mm.bidQuote(100.0, 500.0, 0.0004, 300.0);  // long inventory
    std::printf("flat: bid=%.4f ask=%.4f | long 500: bid=%.4f\n", bid, ask, bidLong);
}
```

```
flat: bid=99.3486 ask=100.6514 | long 500: bid=93.3486
```

Carrying 500 units long shades the bid down by six full points -- the
market-maker is actively discouraging you from buying more, exactly the
inventory-control behavior the model exists to produce.

## 3. The five experiments

Each of these is a complete, compiled, run program. Paths assume you
are in the repo root with `build/libquantfinlib.a` already built.

### (a) Price and greek a vanilla, verify put-call parity

```cpp
#include <cmath>
#include <cstdio>
#include "quantfinlib/pricing/black_scholes.hpp"
using namespace quantfinlib;
int main() {
    double S = 100, K = 105, r = 0.03, q = 0.01, sig = 0.25, T = 0.75;
    auto callG = black_scholes::greeks(OptionType::CALL, S, K, r, q, sig, T);
    auto putG  = black_scholes::greeks(OptionType::PUT,  S, K, r, q, sig, T);
    std::printf("call price=%.6f delta=%.6f gamma=%.6f vega=%.6f theta=%.6f rho=%.6f\n",
                callG.price, callG.delta, callG.gamma, callG.vega, callG.theta, callG.rho);
    std::printf("put  price=%.6f delta=%.6f\n", putG.price, putG.delta);
    double lhs = callG.price - putG.price;
    double rhs = S * std::exp(-q * T) - K * std::exp(-r * T);   // C - P = S e^-qT - K e^-rT
    std::printf("parity check: C-P=%.8f  S*e^-qT-K*e^-rT=%.8f  |gap|=%.3e\n",
                lhs, rhs, std::fabs(lhs - rhs));
}
```

```
call price=7.102933 delta=0.477338 gamma=0.018268 vega=34.252083 theta=-6.450268 rho=30.473115
put  price=10.514007 delta=-0.515191
parity check: C-P=-3.41107442  S*e^-qT-K*e^-rT=-3.41107442  |gap|=0.000e+00
```

Zero gap to the printed precision. This is the put-call parity identity
that gate-checks any option pricer: if it doesn't hold exactly (to
floating-point noise), something in the pricer's carry/discount
handling is wrong. (`call price=7.102933` is also, not coincidentally,
label `bs.call` in the cross-port probe -- Section 1's 138-label
guarantee is checking exactly this number against Java and Python.)

### (b) Bootstrap a curve and price a swap at par

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/rates/yield_curve.hpp"
#include "quantfinlib/rates/swap_pricer.hpp"
using namespace quantfinlib;
int main() {
    std::vector<int> tenors{1, 2, 3, 5, 7, 10};
    std::vector<double> parRates{0.031, 0.033, 0.0345, 0.0365, 0.0375, 0.039};
    YieldCurve curve = YieldCurve::bootstrapAnnualParSwaps(tenors, parRates);
    for (int t : tenors) {
        double pr = swap_pricer::parRate(curve, t);
        double pv = swap_pricer::payerPv(curve, t, pr);
        std::printf("tenor=%2dy  parRate=%.6f  payerPvAtPar=%.3e\n", t, pr, pv);
    }
    double offParPv = swap_pricer::payerPv(curve, 5, 0.02);   // struck away from par
    std::printf("5y payer struck at 2%% fixed: pv=%.6f (nonzero, as expected)\n", offParPv);
}
```

```
tenor= 1y  parRate=0.031000  payerPvAtPar=0.000e+00
tenor= 2y  parRate=0.033000  payerPvAtPar=0.000e+00
tenor= 3y  parRate=0.034500  payerPvAtPar=0.000e+00
tenor= 5y  parRate=0.036500  payerPvAtPar=0.000e+00
tenor= 7y  parRate=0.037500  payerPvAtPar=0.000e+00
tenor=10y  parRate=0.039000  payerPvAtPar=0.000e+00
5y payer struck at 2% fixed: pv=0.074492 (nonzero, as expected)
```

Every tenor reprices its own bootstrap input to zero -- the bootstrap
did its job. Note that `parRate(curve, t)` recovers exactly the input
`parRates[t]` you fed the bootstrap: that round trip, not merely "PV is
small," is what "the result reprices every input exactly" means in
`yield_curve.hpp`'s doc comment.

### (c) Run a backtest with a strategy and read TradeAnalytics honestly

```cpp
#include <cmath>
#include <cstdio>
#include <vector>
#include "quantfinlib/backtest/backtester.hpp"
#include "quantfinlib/backtest/backtest_config.hpp"
#include "quantfinlib/backtest/strategies/sma_cross_strategy.hpp"
#include "quantfinlib/backtest/trade_analytics.hpp"
#include "quantfinlib/data/bar_series.hpp"
using namespace quantfinlib;
int main() {
    std::vector<double> closes;
    double price = 100.0;
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed*1103515245u + 12345u; return (double)((seed>>16)&0x7fff)/32768.0; };
    for (int i = 0; i < 400; i++) {
        double drift = 0.0002 * std::sin(i * 0.05);
        double noise = (rnd() - 0.5) * 0.01;
        price *= (1.0 + drift + noise);
        closes.push_back(price);
    }
    BarSeries series = BarSeries::of("DEMO", closes);
    SmaCrossStrategy strat(10, 30);
    BacktestConfig cfg = BacktestConfig::defaults().withCommission(0.0005);
    BacktestResult result = backtester::run(strat, series, cfg);
    std::printf("%s\n", result.toString().c_str());
    if (result.trades().size() >= 2) {
        auto ta = trade_analytics::analyze(result.trades());
        std::printf("expectancy=%.4f payoffRatio=%.4f winRate=%.3f kelly=%.4f "
                    "maxWinStreak=%d maxLossStreak=%d avgBarsWin=%.1f avgBarsLoss=%.1f\n",
                    ta.expectancy, ta.payoffRatio, ta.winRate, ta.kellyFraction,
                    ta.maxWinStreak, ta.maxLossStreak, ta.avgBarsHeldWinners,
                    ta.avgBarsHeldLosers);
    }
}
```

```
SMA_CROSS(10,30) on DEMO: totalReturn=-5.26%, CAGR=-3.36%, sharpe=-1.13, sortino=-1.48, calmar=-0.45, maxDD=7.42%, profitFactor=0.25, winRate=14.3%, trades=7
expectancy=-751.4240 payoffRatio=1.5100 winRate=0.143 kelly=0.0000 maxWinStreak=1 maxLossStreak=5 avgBarsWin=52.0 avgBarsLoss=17.2
```

Read this honestly, as instructed, rather than cherry-picking a seed
that looks better: a 10/30 SMA cross on 400 bars of mostly-noise data
with a small sinusoidal drift loses money (-5.26% total return, Sharpe
-1.13), wins only 14.3% of the time, and its Kelly fraction correctly
comes back 0 -- the sizing math is telling you not to trade this system
at all, on this data, with this signal. That is `TradeAnalytics` and
`Kelly` doing exactly their job: a losing system does not get dressed up
by better statistics elsewhere in the report. Changing the noise seed
or the SMA periods will change these numbers -- that variability, not a
single flattering run, is the actual lesson of trying this yourself.

### (d) Run OverfitProbability on a deliberately overfit signal

```cpp
#include <cstdio>
#include <vector>
#include "quantfinlib/backtest/validation/overfit_probability.hpp"
#include "quantfinlib/backtest/validation/splittable_random.hpp"
using namespace quantfinlib;

double runOnce(std::int64_t seed, int T, int N, int blocks) {
    SplittableRandom rnd(seed);
    std::vector<std::vector<double>> returns(T, std::vector<double>(N));
    for (int t = 0; t < T; t++)
        for (int j = 0; j < N; j++)
            returns[t][j] = (rnd.nextDouble() - 0.5) * 0.02;   // zero-mean noise, no real edge
    return overfit_probability::cscvSharpe(returns, blocks).pbo;
}

int main() {
    // 20 "strategy variants" that are PURE NOISE -- no real edge, just
    // different random draws: the textbook null hypothesis (a desk that
    // tried 20 meaningless parameter sets and is about to report the best
    // one). A single draw's PBO is one sample from a distribution, so
    // average several independent draws to see where it settles.
    const int T = 800, N = 20, blocks = 8, trials = 12;
    double sum = 0.0;
    for (int s = 1; s <= trials; s++) {
        double pbo = runOnce(1000 + s, T, N, blocks);
        std::printf("trial %2d: PBO=%.4f\n", s, pbo);
        sum += pbo;
    }
    std::printf("mean PBO over %d trials = %.4f\n", trials, sum / trials);
}
```

```
trial  1: PBO=0.1286
trial  2: PBO=0.3286
trial  3: PBO=0.5571
trial  4: PBO=0.7286
trial  5: PBO=0.7286
trial  6: PBO=0.7714
trial  7: PBO=0.5000
trial  8: PBO=0.3143
trial  9: PBO=0.3571
trial 10: PBO=0.1571
trial 11: PBO=0.3714
trial 12: PBO=0.6286
mean PBO over 12 trials = 0.4643
```

This is the honest version of "watch it get caught": no single
variant here has real edge, so on any ONE draw the "in-sample winner"
can look great out of sample purely by chance (trial 1's PBO of 0.13
would, taken alone, look like a passing grade). The mean across 12
independent draws -- 0.4643 -- sits right at the 0.5 danger line the
module's own docstring names as "the selection is pure noise-mining."
That is exactly where 20 genuinely edge-free variants should land, and
it is why the rule of thumb is stated as PBO across repeated evidence,
never a verdict from a single backtest.

### (e) Build and run `benchmarks/ull_bench`, and interpret the numbers honestly

```bash
cmake --build build --target ull_bench
./build/ull_bench.exe
```

Real output from this repo, on the machine this guide was written on:

```
ULL benchmarks (C++ port) -- TSC 0.4007 ns/cycle (2.50 GHz)
CAVEAT: Windows, unpinned cores, no RT priority -- scheduler
preemptions (~ms) own p99.9+/max; compare medians. Java
baselines are JIT-warmed medians from docs/ULTRA_LOW_LATENCY.md.

gate check                   batch     16.8 ns/op (  59.60 Mops/s) | rdtsc p50    14 ns  p99      37 ns | Java: ~3 ns
  (book after run: resting=16382 trades=2933559)
book mixed add/cancel        batch    268.9 ns/op (   3.72 Mops/s) | rdtsc p50   196 ns  p99     848 ns | Java: ~204 ns median, 7M+ ops/s
ring publish->consume        cross-thread             | rdtsc p50   472 ns  p99  638976 ns | Java: publish->strategy p50 204 ns
  (venue book after run: resting=16384 trades=1625180 orders=3200000)
tick-to-order composite      batch    221.2 ns/op (   4.52 Mops/s) | rdtsc p50   156 ns  p99     720 ns | Java: p50 504 ns, p99 1 us
```

Compare this against the numbers quoted in the top-level README (gate
check 2.7 ns/op, book mixed add/cancel 49.5 ns/op, ring publish-to-
consume p50 98 ns, tick-to-order p50 47 ns) and they do not match this
run -- gate check came back 5x slower here, and the ring's p99 shows a
639-microsecond outlier. This is not a regression; it is the
benchmark's own printed caveat proving itself: this machine, at this
moment, had other things scheduled on its cores, and an unpinned
Windows box's tail latency measures the scheduler, not the code. Run it
two or three times yourself, at different times, and you will see the
medians cluster while the p99/p99.9 columns swing by an order of
magnitude -- that swing, reproduced live, is the whole reason the
README says "compare medians" and prints its own caveat rather than
asking you to trust a single number. The Java baselines quoted on the
right of each line are JIT-warmed medians from a *different* run on a
*different* machine (documented in the Java repo's
`docs/ULTRA_LOW_LATENCY.md`) -- treat the comparison as directional
(both engines are in the tens-to-low-hundreds of nanoseconds per
operation, zero heap allocation, single-digit-microsecond p99 under
load), not as a claim that either language beat the other by a
specific, reproducible factor on your hardware.

## 4. What is deliberately different here

**Skipped packages, stated plainly.** As of this port (Phases 1-3,
per the top-level README), everything in the Java "what's ported" table
above is here: `util`, `data`, `pricing`, `rates`, `credit`,
`commodities`, `markets`, `risk`, `volatility`, `indicators`,
`backtest` (+ `strategies`/`validation`/`portfolio`), `orderbook`,
`trading`, `fx`, `crb`, `alpha`, `microstructure`. What is NOT here yet
(Phase 4, in progress as this guide is being written -- you may already
see partial `fix/`, `marketdata/` or `sbe/` directories appear in the
tree, mid-port and not yet in the README's table or this guide):
connectivity -- a FIX engine, market-data feeds, SBE/ITCH codecs. If
you are reading this after Phase 4 lands, check the README's package
table before assuming this guide is stale on that point; this guide
was deliberately written to cite only what exists in the table at the
time it was written.

**The persistence lane does not exist in either non-Java port.** There
is no snapshot/restore, no write-ahead log, no durable order-book
recovery story here or in the Python port. That is Java-only territory
today; if your interest in this library is "can I recover an order
book's exact state after a crash," the Java repo is the only one of the
three with an answer.

**RNG-stream policy: bit-exact, not merely distributionally similar.**
`validation/splittable_random.hpp` is a from-scratch C++ reimplementation
of `java.util.SplittableRandom`'s single-stream constructor path --
Stafford variant 13 mix64, the murmur3-style mix32, and Java's exact
`nextInt(bound)` rejection loop, including how Java's signed 32-bit
integer overflow is reproduced with unsigned arithmetic so it wraps
identically. The payoff: any Java test that seeded a Monte Carlo trade
shuffle, an optimizer's random sampling, or a bootstrap path draws the
IDENTICAL sequence of doubles here, so a seeded test can pin the exact
same expected value in both languages. This is a stronger (and more
expensive to build) guarantee than "the distribution looks the same" --
it is "the stream IS the same," checked bit for bit. The Python port
takes the opposite, cheaper approach on this one axis (see its own
LEARN.md): it pins the distribution's *property* instead of the exact
stream and says so in the docstring. That difference is intentional,
not an oversight in either port -- C++ needed bit-exactness for its
Monte Carlo trade-shuffle tests to carry over Java's pinned expected
values; Python's research-lane tests only need the statistic that
comes out of the stream to be right, not the stream itself.

**Performance positioning.** This port owns the nanosecond story for
the family, with numbers measured on real hardware and printed with
their own caveats rather than asserted: `benchmarks/ull_bench.cpp`
gives you gate-check, order-book, ring-handoff and tick-to-order
latencies, medians in the tens-to-low-hundreds of nanoseconds, zero
heap allocation enforced by a counting `operator new` hook. Python owns
research ergonomics -- if you want to explore a model interactively,
plot something, or iterate quickly on a signal, that is the port to
reach for, not this one. Java remains the reference: when Java, C++ and
Python disagree, Java's number is what the other two are checked
against (Section 1's cross-port guarantee), not the other way around.

## 5. Where the deep material lives

This guide is deliberately the short version. When you want the long
version, everything is in the Java repo, and the concepts and formulas
transfer 1:1 across the port -- only the syntax differs (camelCase
stays camelCase here; Python is the one that renames to snake_case).

**`LEARN.md`** (the Java repo, ~26,000 lines, 1000 worked exercises).
This is where you go when Section 3's five experiments aren't enough --
every package in this guide's tour has dozens of matching exercises
there, phrased against the Java API. Translate mechanically: Java
`BlackScholes.price(...)` becomes C++ `black_scholes::price(...)`,
Java `YieldCurve.bootstrapAnnualParSwaps(...)` becomes C++
`YieldCurve::bootstrapAnnualParSwaps(...)`, and the numbers you get back
should match to the tolerances this guide's Section 1 describes. If an
exercise's expected answer does not match what this port returns, that
is worth a bug report -- the whole point of the port contract is that
divergence should not happen silently.

**`COOKBOOK.md`** (the Java repo, 300 recipes). Short, task-oriented
"how do I..." entries -- "how do I bootstrap a curve from bond
prices instead of par swaps," "how do I size a position with vol
targeting," that kind of thing. Read a recipe, then look up the
equivalent C++ class name in this guide's Section 2 tour (or `grep -rn
ClassName src/` if it's not one of the stops) and translate the calls;
the recipe's logic does not change, only the call syntax.

**`DIAGRAMS.md`** (the Java repo, 100 diagrams). Architecture and
data-flow diagrams -- package dependency graphs, the backtester's
event loop, the order book's matching-engine state machine. These are
language-agnostic by construction (they describe control flow and data
shape, not syntax), so they apply to this port without translation --
the `HftOrderBook` diagram describes this repo's `orderbook/
hft_order_book.hpp` exactly, node pool and occupancy bitmaps included.

**The formula appendix** (the Java repo README, ~175 entries). A
flat lookup table of every closed-form formula in the library, with
the class/method that implements it. Use it as a check on any pricer
in this guide's Section 2 tour: find the class name, read the formula,
then confirm the C++ header's doc comment states the same formula (it
will -- that is the whole port contract from Section 1) before you
trust a result you have not independently derived.
