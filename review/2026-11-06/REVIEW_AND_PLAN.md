# Quant-Finance-CPP v1.0.0: review and improvement plan

Reviewed 2026-11-06 at commit 1b7059e. Nothing in the repo was changed; this file is untracked.

Evidence classes used below:

- **Measured**: reproduced with a test, probe or sanitizer run on Linux (WSL2, GCC 15, Clang 21).
- **Read**: established by reading the code, not executed.
- **Plausible**: likely, but not proven.

Findings are Measured unless marked. Probes are in the session scratchpad and should be
moved into `tests/` as regression tests during Round 1. Timings come from unpinned WSL2,
partly while builds were running, so treat them as indicative. Nothing was run on the
Windows/MinGW setup the README describes.

## 1. Verdict

This is a clean, readable teaching port with a sound ring buffer and matching core. It is
not tradable, and it would not pass review at a bank or HFT firm.

| Area | Grade | Why |
|---|---|---|
| Ring buffers, book matching logic | Good | Acquire/release correct and clean under TSan; no locks or virtuals on the book hot path |
| Latency engineering | Fair | Numbers are real but come from a degenerate workload; layout does not scale |
| Risk controls | Fail | Fail open on NaN, ignore open orders, and are not wired to fills or reference prices |
| Connectivity (FIX, ITCH) | Fail | Remote memory bugs, no authentication, sequence checks can be bypassed |
| Quant library | Teaching grade | Vanilla maths good; several known-wrong formulas are pinned as correct |
| Research and backtest | Weak | Same-bar fills, look-ahead fundamentals, zero default costs |
| Profit | No evidence | No real data, no result, no edge. The docs do not claim one |
| Engineering process | Fail | No CI, red tests on `main`, stale docs, no changelog |

The most serious point is not a single bug. **The C++ port froze at Java v1.17 and
inherited errors that Java has since fixed**, and the parity battery now pins those errors.

## 2. The LinkedIn post needs correcting

These claims are public, and a senior reader can check them from the repo.

| Claim | Verdict | Evidence |
|---|---|---|
| "same machine, same algorithms", "identical hardware" | **False** | Java figures are hard-coded strings in `ull_bench.cpp` (lines 136, 196, 239, 357). The file header says they are "JIT-warmed Linux medians" from the "same hardware class", and LEARN.md says a different run on a different machine. No Java benchmark exists in this repo |
| 730 tests, all green | **False at HEAD** | 80 of 81 executables pass. `exec_routing_test` has 3 failures. There are 742 `TEST(` macros, so 730 is also stale |
| Tick-to-order p50 47 ns (Java 504) | **Number holds, name does not** | Reproduced at about 50 ns. It is one thread with no parsing, network, encoding or core hop. The strategy is a coin flip and 51% of orders are rejected POOL_FULL. That is why it beats the 98 ns ring hop |
| Order book 49.5 ns/op | **Misleading** | 25% of adds are rejected, 84% of cancels miss, and the pool is saturated. A near-touch run with real cancels gave add/cancel p50 of 73/100 ns at 16k orders, 224/425 ns at 1M, and 342/557 ns across 20 books |
| Risk gate 2.7 ns | **Holds narrowly** | It is a batch mean, not a median. About 4.8 ns per fenced call, about 13.6 ns with a concurrent writer. The gate is also missing the checks it needs (section 3) |
| Tick-to-strategy 228 ns | **Not reproducible as benched on Linux** | The bench sets `busySpin=false` (line 255), which gave 39 us p50 on Linux; busy-spin gave 213 ns. Windows behaviour not tested |
| Ring hop 98 ns | **Plausible** | Measured 121 ns unpinned. LEARN.md's own run shows 472 ns |
| "proven equivalent ... 100k random operations" | **Wrong word** | It is a random test. A wrong trade price and swapped maker/taker ids both passed it (unit tests caught them). No IOC, FOK or post-only coverage |
| "venue-grade order book" | **Overstated** | No self-trade prevention, no quantity limit (level quantity overflows), no auction or halt states |
| "full FIX 4.4 session engine" | **False** | Happy path works. See section 3 |
| "ITCH/SBE byte-exact to the Java wire format" | **SBE false today** | C++ matches Java's old schema v1; Java is on v2. The test pins literals, not Java output. ITCH covers 7 of Java's 23 message types |
| Checkpoint byte-identical across three languages | **Overstated** | One-way test reading Python bytes. Nothing compares C++ writer output to Java or Python |
| "every formula, sign, and convention verified" | **False** | 156 labels cover about 100 functions and skip Heston, SABR, vanna-volga, surfaces, swaptions, caps and the binomial tree. Parity proves fidelity to Java, not correctness |
| "complete execution-algo suite", "central risk book" | **Overstated** | Schedulers and trackers with no order lifecycle; a booking and netting calculator with no re-marking |
| "Zero allocation" enforced by counting `operator new` | **Holds** | Also 0 allocations through bus, quoter and gateway, though the test does not cover those paths or aligned `new`/`malloc` |
| 156-value battery has zero mismatches | **Holds** | Rebuilt on the current tree: 156/156. The harness lives in the Python repo, not here |

The post's central lesson, separating language cost from physics, cannot be drawn from
this data: the bus gap also includes a mutex-backed `atomic<shared_ptr>` (see H1), and the
two languages were not measured together. Suggested correction once Round 1 lands:

> Correction to my earlier post: the Java figures were from a separate run, not the same
> machine, and the tick-to-order figure is a single-threaded code path, not wire to wire.
> I have re-run both languages side by side on one pinned Linux box; results and method
> are here: [link].

## 3. Critical findings: fix before anything else

### Risk and order control

- **C1. Position cap ignores open orders** (`hft_risk_gate.cpp:41-55`). With cap 1000, the gate passed 2000 orders of 1000.
- **C2. Quoter stacks orders with no cancel path** (`hft_quoter.cpp:69-72`). 1,001 ticks produced 2,002 live orders against cap 100. The gateway has no cancel, replace, ack or ClOrdID, and is not connected to `FixSession`.
- **C3. Gate is not wired.** No `src` caller of `setReferencePrice` or `HftRiskGate::onFill`. With no reference the collar is skipped, and positions stay 0. All limits default to max.
- **C4. NaN fails open** (`hft_risk_gate.cpp:37,58`). `ref == ref` and `x > limit` both pass on NaN. A NaN tick made the quoter emit 2 NaN orders. Negative prices pass too, and a test pins the unset-reference hole as intended.
- **C5. Kill switch is unsafe.** Queued orders still reach the venue after kill (1000 of 1000). The aggregator clears an operator kill on auto-resume (`global_risk_aggregator.cpp:48-59`). Reducing orders are rejected while killed, and there is no cancel-all or dead-man. Unpriced positions count as zero, so 1e9 shares gave no trip.

### Connectivity

- **C6. Socket close and SIGPIPE on Linux** (`fix_session.cpp:104,419-426`). No `shutdown()`, so `~FixSession` hung; no `MSG_NOSIGNAL`, so a peer FIN killed the process with exit 141. TSan reports races at `close`.
- **C7. Blocking send holds the write lock with no timeout** (`:101-110`). A stalled peer also stops the heartbeat watchdog, and the session stays "established".
- **C8. No authentication or state gate** (`:495`). A NewOrderSingle reached the application before Logon. CompIDs are never checked.
- **C9. SequenceReset bypasses checks** (`:616-622`). A GapFill moved expected seq 2 to 100 with no resend: fills are silently lost.
- **C10. Stale orders can replay** (`:624`). After 141=Y the store is not cleared, so a ResendRequest replayed yesterday's 100,000-share order. An unbounded ResendRequest (7=-9e18) also pins the reader inside the write lock.
- **C11. Memory bugs on untrusted input.** ASan heap overflow at `fix_decoder.cpp:37`; out-of-bounds read at `fix_exec_report_view.cpp:61,64`; signed overflow in parsers; `itch_codec.hpp:104` takes no length. The decoder buffers without limit before logon (256 MB accepted).

### Pricing and risk maths

Measured against QuantLib 1.44, scipy or closed forms.

- **C12. Quanto drift uses the domestic rate** (`quanto_option.cpp:33,45`). Call 10.837 against true 8.430, or +29%.
- **C13. FRTB stressed calibration is the wrong formula** (`frtb_es.cpp:39-49`). Should be ES_R,S x max(1, ES_F,C / ES_R,C). Inputs 70/80/160 give 140 against 160.
- **C14. Expected Shortfall estimator** (`var_engine.cpp:217-233`). No fractional tail weight: a 1000 worst loss gives 505 against 1000 at n=100, c=99%. `risk_metrics` gives a different answer on the same data.
- **C15. `normCdf` is Abramowitz-Stegun, error 7.5e-8** (`math_utils.cpp:364`). N(-9)=0 and N(-8) is 7% off. It flows into every price, and into implied vol (up to 1.3e-4 relative) and Kupiec p-values.

### Process

- **C16. `main` is red, with no CI.** Commit 1b7059e added two tests with inverted fixtures. They expect a BUY limit of 100.05 to refuse a 100.00 ask and take 100.10. **The router code is right; the tests are wrong.** Do not "fix" the code to match them.

## 4. High findings

### Low latency

- H1. "Zero locking" is false: `atomic<shared_ptr>::is_lock_free()` is 0 on libstdc++, with two loads per tick on the bus and gateway path. Possibly a large part of the ring-to-bus gap (Plausible, not isolated).
- H2. Unchecked `symbolId` in gate and bus (Read). Listener exceptions terminate the process (Read).
- H3. Ring slots are SoA: 4-6 cache lines per message (Read). Gate arrays false-share: 2.7 to 10.6 ns with a writer thread.
- H4. Book touches about 12 lines per add (Plausible main cause of the scaling in section 2). Data race on bus counters (TSan).
- H5. Bench uses unfenced `rdtsc` (small effect here, about 2 ns) and subtracts pair overhead from cross-thread deltas. Bench will not compile on MSVC, despite "any C++20 compiler" (Read: GCC builtins and inline asm).

### Connectivity and persistence

- H6. Inbound seq is persisted before dispatch, so a crash loses a fill. No fsync or CRC in the store, and appends after a torn tail lost messages 3 and 4.
- H7. A forced write failure made Checkpoint replace a good 2418-byte file with a truncated 1000-byte one and report success.
- H8. ITCH is unusable on a real feed: no `C` message, no skip length, no MoldUDP64 or gap recovery (Java has it).
- H9. FIX spec gaps: no automatic Reject, garbled messages kill the session, TestReqID unmatched, repeating groups collapse, no TLS.

### Quant

- H10. Negative rates give silent NaN in swaptions and caps; no Bachelier or shifted model.
- H11. Conventions are toy: CDS maturity truncates (1.24y equals 1.0y), bonds round periods (97.22 against 99.15), schedules drift (Aug-31 becomes 28th), no day count or holiday types, single curve only.
- H12. No arbitrage checks. SABR density is negative at 123 of 295 strikes in a test case; calendar arbitrage is accepted. Greeks are NaN at vol=0.
- H13. EWMA seeds from the full sample (look-ahead, Read). GARCH is a grid search.

### Research and execution

- H14. All three bar engines fill at the signal bar's close. Default costs are 0.
- H15. `factors.cpp:52-62` uses one static fundamentals snapshot for all dates. Weights never drift between rebalances, so rebalancing is free. No borrow or financing, no delisting returns.
- H16. Paper gateway fills 100% at touch at any size, and `passesRiskGate` always returns true.
- H17. Algos can overfill: repeat polls gave 500 then 500. POV has the same shape. There is no end-of-day completion (764 of 1000 after 5 polls). A zero VWAP profile sends all 100,000 in slice 0. NaN depth skips the liquidity cap.
- H18. SOR: the new limit and staleness overload has no `src` caller, unset timestamps count as fresh, venue latency is ignored, it sells into a zero bid, and fee-first sorting can trade through displayed price.
- H19. CRB never re-marks, greeks freeze at booking, P&L has no inventory MTM, and JPY is summed with USD.

### Tests

- H20. Line coverage is 89% (13,256 lines, Debug build, GCC), so coverage looks fine and hides the weakness. **7 of 18 mutants survived**, including injected look-ahead in the alpha backtester, sqrt impact turned linear, the PSR kurtosis term and an AC cosh change.
- H21. Lowest coverage: `data` 66%, `rates` 68%, `microstructure` 80%.
- H22. No fuzzing or property tests. FIX tests take 150-247 s.
- H23. Sanitizers: ASan/UBSan ctest shows only the C16 failure, so the memory bugs above are invisible to the suite. TSan: 3 FIX tests fail on races. `lane_risk_aggregator_test` failed once in the parallel TSan run and passed alone, so it is **unexplained** and needs a repeat loop. `ull_allocation_test` does not link under TSan.

## 5. What is good

Keep these and protect them with tests:

- Ring memory ordering, sequence padding, backward-shift delete, atomic FOK.
- Outbound persist-before-send, strict checksum and body-length checks.
- Vanilla BS, Black-76, Margrabe, Kirk, Asian approximations, Vasicek/CIR/Hull-White, Cornish-Fisher.
- Heston agrees with QuantLib to about 1e-8 at normal parameters.
- PSR, DSR, CSCV/PBO, purged K-fold, Almgren-Chriss and Avellaneda-Stoikov read correct.
- Honest comments, and docs that do not claim edge.

## 6. Plan

One PR per round, full docs review before each PR, local tests finished before any push.
Effort is plan-days. At the calibrated 12-15 plan-days per wall-clock day with 2-3 parallel
sessions, the total of about 110 plan-days is 8-10 wall-clock days, excluding CI waits
and the Round 6 data study. Estimates only; actuals to be tracked per round.

| Round | Goal | Work | Exit gate | Plan-days |
|---|---|---|---|---|
| 0. Truth | Stop the claims being wrong | Fix C16 fixtures. CI: GCC, Clang, MinGW; ASan/UBSan; TSan; ruleset. Correct README (Java version, test count, "proven", "full"). Add CHANGELOG, ARCHITECTURE, SECURITY. Owner corrects post | CI green on `main`; no claim without a reproducing command | 6 |
| 1. Fail closed | Make the live lane safe | Order state machine with reservations, cancel/replace, acks, ClOrdID. Fail-closed gate, required limits, wired reference and fills. Kill redesign: per-source latch, cancel-all, reduce-only, dead-man. Quoter cancel-replace. Self-trade prevention. Map controls to RTS 6 and 15c3-5 | Every C1-C5 probe is a regression test; randomised invariant: position plus open never exceeds cap | 20 |
| 2. Connectivity | Survive hostile input | Linux-first non-blocking I/O, bounded queue, `shutdown`, `MSG_NOSIGNAL`. Logon gate, CompIDs, full MsgSeqNum rules, reset clears store, resend limits. Store CRC, fsync, torn-tail repair; atomic Checkpoint. libFuzzer on FIX, ITCH, SBE, Checkpoint. Full ITCH plus MoldUDP64. Interop test against QuickFIX | Fuzz clean for 1 CPU-hour per target in nightly; kill -9 crash test loses no fills; QuickFIX interop passes | 22 |
| 3. Maths | Correct, not just matching | Cody normCdf, Halley normInv, quanto, FRTB, ES, then Java 2.0-4.0 re-baseline: multi-curve, DayCount, calendars, ISDA CDS, Bachelier, 8 barriers, SVI, arbitrage checks. Do not port SIMM (licensing). QuantLib golden CSVs in this repo. Move crossport here and widen it | Golden tests independent of Java; parity harness runs in CI; no NaN on documented domains | 28 |
| 4. Latency proof | Numbers that survive scrutiny | Real workload: replay ITCH, near-touch flow, 1M orders, many books. One 64-byte ring slot; AoS book nodes; drop `atomic<shared_ptr>`. Fenced timing, pinned cores, full percentiles, HDR histograms. Java and C++ run together on one Linux host. Wire-to-wire loopback path with parsing and encoding | Published method, hardware and raw data; each claim has a reproducing script; perf regression gate on a quiet runner | 14 |
| 5. Research honesty | Backtest cannot flatter | Next-open default, non-zero default costs, as-of fundamentals, drift and rebalancing cost, borrow, delisting. Queue-based fill simulator with latency. Algo in-flight accounting, EOD completion, limits, lots. SOR latency, stale fail-closed, trade-through guard. CRB re-mark and currency conversion | Look-ahead mutant is killed; mutation score at least 85% in core quant, run nightly | 16 |
| 6. Evidence | Show something real | Pre-registered study on free data only: execution quality or queue model on IEX, which already sits in the wider project. Report nulls as nulls. No Nasdaq Data Link or LOBSTER sign-ups | Result published with costs and a held-out period, whatever it says | 4 + data time |

Rounds 1, 2 and 3 touch separate packages and can run in parallel after Round 0.
Round 4 needs Rounds 1 and 2. Round 5 can start with Round 3.

**Tradability and profit:** nothing here makes money by itself. C++ speed buys nothing
without colocation, direct feeds and an edge, and the owner's earlier studies on free data
found no edge. Realistic value is as a credible, verified engineering and research asset.
Round 6 should be expected to give a null, and that is an acceptable result.

## 7. Owner decisions

1. **Identity.** Recommended: "verified research and engineering reference, not production". Claiming production would add venue certification and operations work with no venue to connect to.
2. **Java relationship.** Recommended: re-baseline selected Java 4.0 fixes, then treat C++ as independent, with parity kept only for the maths in Round 3. Full class-for-class parity costs more and adds little.
3. **Platform.** Recommended: Linux first, with MinGW kept as a CI leg. Low-latency claims should be Linux only.
4. **LinkedIn post.** Recommended: correct it now on the same-machine and all-green claims, rather than waiting for Round 4 numbers.
5. **Benchmark host.** A pinned bare-metal Linux box is needed for Round 4. WSL2 is not good enough. Any paid host needs an owner decision; zero spend means dual-booting or using an existing machine.
