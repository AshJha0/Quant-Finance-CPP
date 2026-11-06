# Learning quantfinlib in C++

A guide to the C++20 library in this repository: how it is organised, a
reading path through its packages, and experiments you can compile and
run. It began as a port of
[Quant-Finance-Library](https://github.com/AshJha0/Quant-Finance-Library)
(Java) and has since moved away from it in places, listed in section 4.

Every complete program below was extracted from this file, compiled and
run on 2026-11-06 with g++ 15.2 on Ubuntu under WSL2, against the commit
this file ships in. The outputs are pasted from those runs. Benchmark
figures will differ on every run: see experiment (e).

How the running system fits together is in
[ARCHITECTURE.md](ARCHITECTURE.md). Which regulatory controls have code
behind them is in [CONTROLS.md](CONTROLS.md). This is a research and
engineering reference, not a production trading system.

## 1. How the code is written

### Ancestry, and where it stops

Most classes started as method-for-method ports of the Java library, so
names stay camelCase (`black_scholes::price`, not
`black_scholes::compute_price`), and Java static classes became free
functions in a matching namespace, or static methods when the original
held state. The enum `OptionType` sits at `quantfinlib::` scope because
every pricing header needs it.

Agreement with Java is no longer the test of correctness. A number that
matches Java can still be wrong, and several were until they were fixed
here: the normal CDF, the quanto drift, FRTB stressed calibration and
Expected Shortfall. Where this library has an independent reference,
the reference wins. `tests/golden_independent_test.cpp`,
`tests/golden_models_test.cpp` and `tests/golden_isda_test.cpp` hold
those values.

### Exception mapping

Bad arguments throw `std::invalid_argument`, with the bad value in the
message. A misused object, such as an empty `BarSeries` builder, throws
`std::logic_error`. Corrupt files and I/O failures throw
`std::runtime_error`. There is no library-wide exception hierarchy.

The hot trading path differs: `HftOrderBook`, `HftRiskGate` and
`HftOrderGateway` return codes for a bad order, because a matching or
order loop must not unwind over one.

### NaN gates

Input checks are written `if (!(x > 0))`, not `if (x <= 0)`. Every
comparison with NaN is false, so the negated form catches NaN and
non-positive values in one branch, and the second form lets NaN through.
On the order path this decides whether a NaN price can reach a venue, so
`HftRiskGate` validates the price before any limit.

### Bracket-checked solvers

The root-finders (implied vol, bond YTM, credit-curve hazard bootstrap)
check the bracket before they start instead of trusting the loop to
converge. `black_scholes::impliedVol` returns NaN when a price is not
attainable inside its vol bracket, so a stale quote surfaces as "no vol"
and not as the search bound. `CreditCurve::bootstrap` searches `h` in
`[1e-9, 10]`, and a quote no hazard can explain throws.

### Fail closed

The trading packages share one rule: if the code cannot tell that an
action is safe, it refuses. A fresh `HftRiskGate` rejects every order
until limits and a reference are set. A position nobody can price trips
the firm-wide breaker. A fill nobody can account for latches a kill.
Stop 11 and experiments (f) and (g) show this working.

### Tests

`ctest --test-dir build` runs one executable per `tests/*_test.cpp`. The
pricing, rates, credit and risk tests pin literal expected doubles.
Continuous integration (`.github/workflows/ci.yml`) builds with GCC,
Clang and MinGW, and runs AddressSanitizer with UBSan, and
ThreadSanitizer. libFuzzer targets for the FIX decoder, ITCH, SBE and
Checkpoint are under `fuzz/`.

## 2. A guided tour in 14 stops

Read these in order; each one assumes the previous. Build the library
once, on Linux:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target quantfinlib
```

Then compile any snippet from the repository root:

```bash
g++ -std=c++20 -O2 -I src t.cpp build/libquantfinlib.a -pthread -o t && ./t
```

### Stop 1 -- `util`: the numerical bedrock

**What it does.** `math_utils` (`src/quantfinlib/util/math_utils.hpp`):
the primitives every other package leans on: mean/variance/stdDev,
percentile, covariance/correlation, Cholesky, the Cody `normCdf`, a
Halley-refined Acklam `normInv`, skewness/kurtosis, a NaN-safe `pairSort`.

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

### Stop 3 -- `pricing`: Black-Scholes and its siblings

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

**Try it:** Experiment (a) below prices a vanilla. A quanto shows why
each rate has one job. The payoff is on a foreign asset, settled in
domestic currency at a fixed conversion rate. Formally, under the
domestic measure the asset drifts at `r_f - q - rho*sigmaS*sigmaFX`, and
the value is `e^(-r_d T) * Black76(F_quanto, K, sigmaS, T)`, so the
foreign rate only enters the forward and the domestic rate only
discounts (Hull, the quanto section; Wystup 2006, 2.6):

```cpp
#include <cstdio>
#include <initializer_list>
#include "quantfinlib/pricing/quanto_option.hpp"
using namespace quantfinlib;
int main() {
    double S = 100, K = 100, rd = 0.05, rf = 0.01, q = 0.0, sS = 0.2, sFx = 0.1, T = 1.0;
    for (double rho : {-0.5, 0.0, 0.5}) {
        std::printf("rho=%+.1f forward=%.4f call=%.4f\n", rho,
                    quanto_option::quantoForward(S, rf, q, sS, sFx, rho, T),
                    quanto_option::price(OptionType::CALL, S, K, rd, rf, q, sS, sFx, rho, T));
    }
}
```

```
rho=-0.5 forward=102.0201 call=8.6525
rho=+0.0 forward=101.0050 call=8.1026
rho=+0.5 forward=100.0000 call=7.5771
```

Positive correlation lowers the forward: the asset tends to be high when
the foreign currency is strong, and the fixed conversion gives that
benefit away.

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

**Read first:** `ewma_volatility.hpp`, the simplest model in the
package: one parameter, `lambda`, and causal seeding, so `h_t` sees only
returns before `t`.

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
latestVol=0.016403 annualizedVol=0.2604
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
backtesting. A signal decided on bar t's close fills at bar t+1's open,
the earliest price a real order can get. Filling on the signal bar needs
`withSameBarCloseFill()`, which says what it is. Defaults are not free:
`commissionRate` 0.001, `slippageRate` 0.0005, and each fill capped at
10% of the bar's volume. Stop-loss and take-profit are checked intrabar
with gap-aware fills. Five strategies ship in `strategies/`: SMA cross,
EMA cross, RSI, MACD, Bollinger. Also here: cost models, execution
models, walk-forward with warm folds, grid search.

**Read first:** `backtester.hpp`, then `strategies/
sma_cross_strategy.hpp` as the simplest concrete `TradingStrategy`.

**Try it:** see Experiment (c) below.

### Stop 10 -- `backtest/validation` and `backtest/portfolio`: does the edge survive scrutiny?

**What it does.** `validation/`: purged K-fold with embargo (the fix
for financial-label leakage), CSCV overfit probability, block
bootstrap, deflated Sharpe, Monte Carlo trade reshuffle, seeded through
`SplittableRandom` (`validation/splittable_random.hpp`). `portfolio/`: Kelly and fixed-fractional position
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

### Stop 11 -- `orderbook` + `trading`: the order path

**What it does.** `HftOrderBook` (`orderbook/hft_order_book.hpp`) is a
price-time matching engine on a dense integer-tick ladder, with pooled
order nodes and no allocation after construction, checked by a counting
`operator new` test. `HftRiskGate` is the pre-trade gate.
`HftOrderGateway` is the order state machine behind it, and
`OrderRingBuffer` the hand-off to the venue thread.

**The idea.** A gate that looks only at the current position can be
walked past: send ten orders before any fill, and each one sees position
zero. So the gate must count what is already working.

**Formally.** With `openBuy` and `openSell` the quantities reserved by
working orders, the gate holds

    position + openBuy <= cap        position - openSell >= -cap

`check()` asks and reserves nothing. `reserve()` asks and takes the
quantity. Only `reserve()` keeps the inequality with several orders in
flight, so `HftOrderGateway::submit` uses it, and gives quantity back on
fill, cancel and reject.

**Worked example.** A fresh gate rejects: every limit is zero and the
collar is unconfigured. With a cap of 1500, two checks of 1000 both
pass, because neither took anything. The second of two reserves fails.

**Read first:** the comment at the top of `hft_risk_gate.hpp`, then the
one in `hft_order_gateway.hpp`, then `hft_order_book.hpp`.

**Try it:**

```cpp
#include <cmath>
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

    auto name = [](int code) { return HftRiskGate::reasonName(code); };
    HftRiskGate gate(4);
    std::printf("fresh gate: %s\n", name(gate.check(0, Side::BUY, 100, 100.0)).c_str());

    gate.maxOrderQuantity(1000).maxOrderNotional(1e6).maxPositionQuantity(1500)
        .priceCollarPct(0.05);
    std::printf("limits, no reference: %s\n", name(gate.check(0, Side::BUY, 100, 100.0)).c_str());

    gate.setReferencePrice(0, 100.0);
    int c1 = gate.check(0, Side::BUY, 1000, 100.5);
    int c2 = gate.check(0, Side::BUY, 1000, 100.5);
    int r1 = gate.reserve(0, Side::BUY, 1000, 100.5);
    int r2 = gate.reserve(0, Side::BUY, 1000, 100.5);
    std::printf("check,check: %s,%s  reserve,reserve: %s,%s  openBuy=%lld\n",
                name(c1).c_str(), name(c2).c_str(), name(r1).c_str(), name(r2).c_str(),
                (long long)gate.openQuantity(0, Side::BUY));
    std::printf("at 200.0: %s  at NaN: %s\n", name(gate.check(0, Side::BUY, 10, 200.0)).c_str(),
                name(gate.check(0, Side::BUY, 10, std::nan(""))).c_str());
}
```

```
bidId=1 askId=2 bestBid=9990 bestAsk=10010
fresh gate: MAX_ORDER_QTY
limits, no reference: PRICE_COLLAR
check,check: OK,OK  reserve,reserve: OK,MAX_POSITION  openBuy=1000
at 200.0: PRICE_COLLAR  at NaN: INVALID_PRICE
```

Nothing sets the reference for you on a standalone gate: wire your feed
to `setReferencePrice`. `ShardedTradingEngine::publish` and
`PaperTradingGateway::onQuote` do it themselves.

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

**What it does.** `alpha`: a full research pipeline (context, factor
generators, a signal evaluator, purge-aware validation, a
backtester, portfolio construction, a report), Fama-MacBeth, calendar
anomalies. `microstructure`: Almgren-Chriss optimal execution, TCA,
market-impact models, per-bucket seasonality curves, queue/fill models,
Lee-Ready trade classification, VPIN, Hawkes intensity, EWMA
covariance, Avellaneda-Stoikov optimal market-making, an alpha
ensemble, an online learner.

**Read first:** `microstructure/avellaneda_stoikov.hpp` -- short,
self-contained, and a clear example of a units contract (it states that
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

### Stop 14 -- `fix`, `marketdata`, `sbe`, `persist`: the edges

**What it does.** `fix`: a FIX 4.4 session over TCP, initiator or
acceptor, with a logon gate, CompID checks, MsgSeqNum rules, resend
handling and a durable `FileSessionStore`. There is no TLS.
`trading/fix_venue_bridge.hpp` connects it to the gateway. `marketdata`:
the tick bus, an ITCH codec, a MoldUDP64 receiver, an L3 book builder.
`sbe`: flyweight codecs. `persist`: `DurableFile` (checked writes,
fsync, CRC-32) and `Checkpoint`, a file of named sections for learned
state.

**Read first:** the top of `fix/fix_session.hpp`, then
`trading/fix_venue_bridge.hpp`, then [ARCHITECTURE.md](ARCHITECTURE.md).

**Try it:** a checkpoint commits on `close()`, by writing a temporary
file and renaming it over the old one, so a crash mid-save leaves
yesterday's file whole.

```cpp
#include <cstdio>
#include "quantfinlib/persist/checkpoint.hpp"
using namespace quantfinlib;
int main() {
    const char* path = "demo.qflc";
    Checkpoint::Writer w = Checkpoint::writer(path);
    w.section("model.demo", [](BinWriter& o) { o.writeDouble(0.125); });
    w.close();
    double restored = 0;
    Checkpoint::Reader r = Checkpoint::reader(path);
    r.section("model.demo", [&](BinReader& i) { restored = i.readDouble(); });
    std::printf("restored=%.3f\n", restored);
    std::remove(path);
}
```

```
restored=0.125
```

## 3. The experiments

Each of these is a complete program. Compile as shown at the start of
section 2.

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
call price=7.102945 delta=0.477338 gamma=0.018268 vega=34.252083 theta=-6.450267 rho=30.473110
put  price=10.514020 delta=-0.515190
parity check: C-P=-3.41107442  S*e^-qT-K*e^-rT=-3.41107442  |gap|=0.000e+00
```

Zero gap to the printed precision. This is the put-call parity identity
that gate-checks any option pricer: if it doesn't hold exactly (to
floating-point noise), something in the pricer's carry/discount
handling is wrong.

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

### (c) Run a backtest and read TradeAnalytics plainly

The same strategy on the same series, first with the defaults, then
with the fill moved to the signal bar's close and costs set to zero.

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
    BacktestConfig cfg = BacktestConfig::defaults();
    BacktestResult result = backtester::run(strat, series, cfg);
    std::printf("defaults:  %s\n", result.toString().c_str());
    auto ta = trade_analytics::analyze(result.trades());
    std::printf("expectancy=%.4f payoffRatio=%.4f winRate=%.3f kelly=%.4f maxLossStreak=%d\n",
                ta.expectancy, ta.payoffRatio, ta.winRate, ta.kellyFraction, ta.maxLossStreak);
    BacktestConfig flattering = cfg.withSameBarCloseFill().withCommission(0).withSlippage(0);
    SmaCrossStrategy strat2(10, 30);
    std::printf("flattered: %s\n", backtester::run(strat2, series, flattering).toString().c_str());
}
```

```
defaults:  SMA_CROSS(10,30) on DEMO: totalReturn=-6.83%, CAGR=-4.37%, sharpe=-1.44, sortino=-1.85, calmar=-0.49, maxDD=8.90%, profitFactor=0.16, winRate=14.3%, trades=7
expectancy=-975.6603 payoffRatio=0.9305 winRate=0.143 kelly=0.0000 maxLossStreak=5
flattered: SMA_CROSS(10,30) on DEMO: totalReturn=-4.59%, CAGR=-2.93%, sharpe=-0.99, sortino=-1.30, calmar=-0.42, maxDD=6.95%, profitFactor=0.29, winRate=14.3%, trades=7
```

Read this plainly. A 10/30 SMA cross on 400 bars of mostly noise loses
6.83% at the defaults, wins 14.3% of its trades, and its Kelly fraction
comes back 0: the sizing maths says not to trade it. Same-bar fills with
no costs recover 2.24 points of that with the same seven trades, and
that gap is pure flattery, since no real order gets those fills. A
different seed or different periods will change every number here.

### (d) Run OverfitProbability on pure noise

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

No variant here has any edge, so on one draw the in-sample winner can
look fine out of sample by chance: trial 1's PBO of 0.13 would, alone,
look like a pass. The mean across 12 independent draws, 0.4643, sits
near 0.5, where selection carries no information. PBO is evidence to
collect over repeated samples, not a verdict from one backtest.

### (e) Run `benchmarks/ull_bench` and read it with care

```bash
cmake --build build --target ull_bench
./build/ull_bench
```

An excerpt from one run. **Indicative only: WSL2, unpinned, on a shared
desktop.** No figure here is a claim about any other host.

```
cpu:      Intel(R) Core(TM) 5 120U, 12 logical
virtual:  WSL2. vCPUs float over physical cores, so pinning here pins a
          vCPU only, and the Windows host can preempt it at any time.
pinning:  NONE. Threads float; expect the tail to measure the scheduler.
timer:    fenced rdtsc/rdtscp, 0.4006 ns/tick, floor (empty pair) 49.7 ns, not subtracted

stage (ns)                                           n     mean     p50     p90     p99
gate check, one call, fenced                   2000000     61.7      53      70      70
  unfenced batch mean 12.7 ns/call: a throughput figure, calls overlap
book 16k add                                   2019324    136.5     114     196     312
book 16k cancel                                1780706    195.1     172     280     424
book 1M add                                    2019555    371.0     328     472     624
book 1M cancel                                 1780514    856.5     784    1120    1376
ring hand-off, busy-spin consumer               500000  25481.7     328     424  368640
bus publish->listener, BUSY-SPIN consumer       500000   6027.5     360     472  135168
bus publish->listener, PARKED consumer          200000  76973.3   64512  120832  499712
single-thread code path (no I/O, no core hop)   2000000    157.4     140     196     280
```

Other builds were running during this run (load average about 9), so
these are worse than a quiet host would give. Four things to take from
it:

- The fenced gate figure, 53 ns, sits on the 49.7 ns timer floor, so it
  says only that one call is below what this timer resolves.
- The book slows as it grows: an add costs about three times as much at
  1M resting orders as at 16k. Quote the size with the number.
- A busy-spin consumer and a parked one differ by two orders of
  magnitude. A bus figure without that label means nothing.
- The hand-off p99 is hundreds of microseconds, and the mean is far above
  the p50. That tail belongs to the host, as the bench's hiccup monitor
  shows: its own p99 stall was 2.29 ms. Use the medians here.

The last stage is one thread running ring, strategy, gate, order ring
and book. It has no parsing, network, encoding or core hop, so it is the
cost of a code path and not a latency from any wire.

### (f) Follow one order through its life

**The idea.** An order is not finished when it is sent. Until the venue
answers, the quantity could still fill, so it stays reserved.

**Formally.** `QUEUED` (in the ring), `SENT` (delivered, no ack), `LIVE`
(acked), then done, when the reservation is released. Reservation is the
quantity that can still fill. While a replace is pending it is
`max(old, new)` less what has filled, because either could apply until
the venue answers. `replace` takes the new leaves; the wire message
carries the FIX total, including what has filled.

**Worked example.** `pollOnce()` stands in for the venue thread and the
`on*` calls stand in for the FIX bridge, so the run is deterministic.

```cpp
#include <cstdio>
#include "quantfinlib/trading/hft_order_gateway.hpp"
using namespace quantfinlib;
static const char* kType[] = {"NEW", "CANCEL", "REPLACE"};
int main() {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e6).maxPositionQuantity(1500)
        .priceCollarPct(0.05);
    gate.setReferencePrice(0, 100.0);
    HftOrderGateway gw(1024, gate, false);
    gw.addVenueListener([](const VenueMessage& m) {
        std::printf("  wire: %s qty=%lld px=%.2f\n", kType[(int)m.type],
                    (long long)m.quantity, m.price);
    });
    auto show = [&](const char* what) {
        std::printf("%-16s position=%lld openBuy=%lld\n", what, (long long)gate.position(0),
                    (long long)gate.openQuantity(0, Side::BUY));
    };
    std::int64_t id = gw.submit(0, Side::BUY, 1000, 100.0, 1);
    show("submit 1000");
    std::int64_t second = gw.submit(0, Side::BUY, 1000, 100.0, 2);
    std::printf("second submit: %s\n", HftRiskGate::reasonName((int)-second).c_str());
    gw.pollOnce();                 // the venue thread delivers: SENT
    gw.onOrderAck(id);             // LIVE
    gw.onFill(id, 0, Side::BUY, 400);
    show("fill 400");
    std::int64_t rep = gw.replace(id, 300, 100.5, 3);   // 300 left working
    gw.pollOnce();
    show("replace pending");
    gw.onReplaceAck(rep);
    show("replace acked");
    std::int64_t can = gw.cancel(id, 4);
    gw.pollOnce();
    gw.onOrderOut(can);
    show("cancelled");
}
```

```
submit 1000      position=0 openBuy=1000
second submit: MAX_POSITION
  wire: NEW qty=1000 px=100.00
fill 400         position=400 openBuy=600
  wire: REPLACE qty=700 px=100.50
replace pending  position=400 openBuy=600
replace acked    position=400 openBuy=300
  wire: CANCEL qty=700 px=100.50
cancelled        position=400 openBuy=0
```

Read the reservation column. The refused second submit shows the cap
counting a working order at position 0. After the fill, 400 moved from
open to position. During the pending replace, open stays at 600, the
larger of old and new, and drops to 300 only on the ack. The replace
went out with quantity 700: 400 filled plus 300 leaves. The cancel gives
back the last 300, and the position stays, because a fill is a fact.

### (g) Trip a kill and see what clears it

**The idea.** A kill must not undo itself, and one party must not be
able to clear another's reason for stopping.

**Formally.** `killMask` holds one latched bit per source:
`KILL_OPERATOR`, `KILL_AGGREGATOR`, `KILL_DEAD_MAN`, `KILL_GATEWAY`,
`KILL_VENUE_LINK`. A source clears only its own bit, and nothing clears
on a timer. While any bit is set, only an order that strictly reduces
the position passes. `GlobalRiskAggregator::manualReset()` clears the
aggregator's bit, and refuses while gross is at or above
`cap x resumeFraction` or any position has no price.

**Worked example.** 1000 shares at 100 is 100,000 gross against a cap of
50,000, so the resume level is 45,000.

```cpp
#include <chrono>
#include <cstdio>
#include <thread>
#include "quantfinlib/trading/global_risk_aggregator.hpp"
#include "quantfinlib/trading/hft_risk_gate.hpp"
using namespace quantfinlib;
int main() {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e6).maxPositionQuantity(5000)
        .priceCollarPct(0.05);
    gate.setReferencePrice(0, 100.0);
    gate.onFill(0, Side::BUY, 1000);
    GlobalRiskAggregator agg({&gate}, 50000.0, 0.9, 1'000'000);
    while (!agg.isTripped()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    gate.kill(true);
    std::printf("mask=%u\n", gate.killMask());
    std::printf("buy: %s  sell: %s\n",
                HftRiskGate::reasonName(gate.check(0, Side::BUY, 10, 100.0)).c_str(),
                HftRiskGate::reasonName(gate.check(0, Side::SELL, 10, 100.0)).c_str());
    std::printf("manualReset at gross 100000: %d\n", agg.manualReset());
    gate.onFill(0, Side::SELL, 700);
    bool cleared = agg.manualReset();
    std::printf("manualReset at gross 30000: %d  mask=%u\n", cleared, gate.killMask());
    gate.kill(false);
    std::printf("after operator reset: mask=%u killed=%d\n", gate.killMask(), gate.isKilled());
}
```

```
mask=3
buy: KILLED  sell: OK
manualReset at gross 100000: 0
manualReset at gross 30000: 1  mask=1
after operator reset: mask=0 killed=0
```

Mask 3 is the aggregator (2) and the operator (1). A buy is refused and
a sell passes, because it strictly reduces the position. The first reset
is refused at gross 100,000. At 30,000 it succeeds and clears only its
own bit, so the operator's kill stands until the operator lifts it.

### (h) Paper trade through the same gate

`PaperTradingGateway` takes a gate in its constructor; running without
one needs the factory `unsafeWithoutRiskGate`. Fills come from a
`QueueFillSimulator` per symbol: an aggressive order takes only displayed
depth and a market remainder is cancelled. Time is event time, so an
order reaches the simulated venue when a later feed event moves the
clock past its arrival.

```cpp
#include <cstdio>
#include "quantfinlib/trading/paper_trading_gateway.hpp"
using namespace quantfinlib;
int main() {
    HftRiskGate gate(4);
    gate.maxOrderQuantity(1000).maxOrderNotional(1e6).maxPositionQuantity(5000)
        .priceCollarPct(0.05);
    PaperTradingGateway paper(1'000'000.0, 0.0005, gate, {{"DEMO", 0}},
                              PaperTradingGateway::FillConfig::zeroLatency());
    paper.onQuote("DEMO", 1000, 99.99, 500, 100.01, 200);   // also sets the reference
    paper.submitLimit("DEMO", Side::BUY, 5000, 100.01);     // above maxOrderQuantity
    std::int64_t id = paper.submitMarket("DEMO", Side::BUY, 300);
    paper.onQuote("DEMO", 2000, 99.99, 500, 100.01, 200);
    std::printf("position=%.0f cash=%.2f cancelledRemainder=%d\n", paper.position("DEMO"),
                paper.cash(), paper.status(id) == OrderStatus::CANCELED);
    for (const std::string& line : paper.rejectionLog()) std::printf("log: %s\n", line.c_str());
}
```

```
position=200 cash=979988.00 cancelledRemainder=1
log: order 1: MAX_ORDER_QTY for DEMO
```

The order above `maxOrderQuantity` is refused by the gate and logged.
The market order for 300 meets 200 displayed, so 200 fill at 100.01 and
the remainder is cancelled. Cash falls by 20,002 plus 10 of commission.

## 4. Where this differs from the Java library

**Deliberate maths changes.** `normCdf` is Cody's rational approximation
and `normInv` adds a Halley step; the quanto takes a foreign rate and
drifts on it; FRTB stressed calibration, Expected Shortfall and EWMA
seeding were corrected. Do not expect Java's digits on these.

**Backtest defaults.** Next-open fills and non-zero costs.
`backtest_config.hpp` records the deviation.

**The live lane.** Reservations, the order state machine, latched kills,
self-trade prevention and the FIX bridge were reworked here. See
[ARCHITECTURE.md](ARCHITECTURE.md).

**Persistence.** `Checkpoint` keeps the Java framing, except that section
names are plain UTF-8. `FileSessionStore` has a private format with CRCs
and is not compatible with the Java store.

**Random numbers.** `validation/splittable_random.hpp` reimplements
`java.util.SplittableRandom`'s single-stream path, so seeded shuffles and
bootstraps draw the same sequence as Java.

**What is not here.** No venue certification, no TLS, no order audit
log, no order status query after a reconnect. Nothing in this library is
evidence of a trading edge.

## 5. Where more material lives

The Java repository has a much longer `LEARN.md`, a `COOKBOOK.md` and
`DIAGRAMS.md`. The concepts carry over, and calls translate mechanically
(Java `BlackScholes.price(...)` is `black_scholes::price(...)`). Check
any answer against section 4 first: a difference on those topics is
expected, and Java diagrams of the order book or gateway describe the
Java layout, not this one.
