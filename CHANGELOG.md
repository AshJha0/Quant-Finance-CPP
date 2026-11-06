# Changelog

## Unreleased

- The language standard is now C++26, so a compiler that accepts `-std=c++26` is required. Only features our compilers support are used; see the README.
- `std::to_string(double)` now prints the shortest form (`1.1`, not `1.100000`) on current standard libraries. In the library this changes 124 error messages and 6 display strings, such as FX `toString` and the `BOLL_REV` factor name. No FIX output, file format or calculation uses it.

## 2.0.0 (2026-11-06)

Follows the review in `review/2026-11-06/REVIEW_AND_PLAN.md`. This is a breaking release: defaults now fail closed and backtests are more conservative, so results will fall.

### Risk and orders
- `HftRiskGate` fails closed: a fresh gate rejects everything until limits and a reference are set, or `disableReferenceCheck()` is called. NaN, non-positive and non-finite prices are rejected.
- Open quantity is reserved by `reserve()`, and position plus open cannot exceed the cap. `check()` does not reserve.
- Order state machine with ClOrdID, ack, cancel, cancel-replace, overfill and unsolicited-fill handling. `startOrderIdsAt` and `nextOrderId` stop ClOrdIDs repeating across restarts.
- Kill is per source and latched, stops queued orders, allows reduce-only, and has cancel-all and a dead-man. The aggregator no longer auto-resumes: use `manualReset()`. `kill(bool)` touches the operator bit only.
- `trading/fix_venue_bridge` connects the gateway to FIX. Disconnect trips cancel-all and the `KILL_VENUE_LINK` source.
- Self-trade prevention. `HftQuoter` cancel-replaces instead of stacking.

### Connectivity and persistence
- FIX: Linux-safe close and sends, bounded send, Logon required first, CompID checks, full MsgSeqNum rules, store reset on 141=Y, stale replay gap-filled, ResendRequest validated, automatic Reject, TestReqID matching, repeating groups.
- Stricter than FIX by default: SequenceReset-Reset is refused unless `allowSequenceResetMode` is set, and GapFill is accepted only within a resend.
- Parsers are length-checked with overflow-safe numbers. `accept` and `initiate` throw if logon fails.
- `FileSessionStore` format changed: old stores will not open. It now has CRC, fsync and torn-tail repair. Per-write fsync lowers throughput.
- Checkpoint writes atomically and adds a trailing `__crc32` section, still format 1.
- ITCH gains `C` and skip lengths. New MoldUDP64 receiver. New IEX-TP, DEEP and DEEP+ decoder, books and `iex_replay` tool.
- libFuzzer targets under `QFL_FUZZ`.

### Maths
- Cody `normCdf` and refined `normInv`: prices move by about 1e-5. Quanto takes `foreignRate`. FRTB stressed calibration corrected. ES unified on Acerbi-Tasche.
- Added: Bachelier and shifted models, `DayCount`, schedule anchors and stubs, accrued, holiday calendars, multi-curve OIS bootstrap, ISDA CDS, SVI, GARCH(1,1) MLE, all 8 barriers, causal EWMA, surface arbitrage checks with opt-in `rejectArbitrage()`.
- Independent golden tests from QuantLib 1.44, scipy and mpmath. Cross-port labels for these functions are now expected to differ from Java v1.17.

### Research and execution
- Backtests fill at next open by default (`withSameBarCloseFill()` to opt out), with non-zero costs, as-of fundamentals, weight drift, and assumed borrow (50 bps) and financing (5%).
- Delistings and mergers through shared `corporate_actions::schedule`.
- `QueueFillSimulator`: queue, latency (assumed 500 us order, 100 us feed), partials, mark-outs. `PaperTradingGateway` uses it and requires a gate (`unsafeWithoutRiskGate` opts out).
- Algos: `onSent` and `onCancelled` must be called, because polling does not reserve. Horizon completion and fail-closed inputs. Zero weights throw.
- SOR: `ChildOrderRouter` passes limit and quote age. Unguarded overloads are deprecated.
- CRB re-marks, has inventory MTM, and needs FX rates before summing. Renamed: `AlphaContext::ofUndatedSnapshot`, `fundamentalsAsOf`, `universeForEngine`.

### Latency
- Bus and gateway no longer use `atomic<shared_ptr>`. 64-byte ring slots (twice the ring memory). AoS book nodes. Book quantity limit (`REJECT_QUANTITY_LIMIT`). Gate padded per symbol.
- Benchmarks rewritten: fenced timing, real cancels, no POOL_FULL, full percentiles, no hard-coded Java numbers.

### Process
- CI: GCC, Clang, ASan/UBSan, TSan, MinGW. Test fixtures that inverted a buy limit corrected.
