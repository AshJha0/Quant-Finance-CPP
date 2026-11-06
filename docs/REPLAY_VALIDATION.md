Data provided for free by IEX. By accessing or using IEX Historical Data, you agree to the IEX Historical Data Terms of Use.

Terms: <https://www.iex.io/products/market-data-connectivity/hist-terms>

# Replay validation on real IEX messages

This is a correctness check of the parser, the books and the queue estimator on real feed messages. It is not an edge or profit study: nothing here says whether any strategy makes money, and no result should be read that way.

**Limits.** One trading day, 12 December 2025, on one venue with a small share of volume. The run was made once. Aggregate counts only are published; no prices, quotes or per-symbol statistics. IEX notes its data does not reflect trading on other markets. No Nasdaq data was used.

## What was run

Two HIST files for the same day, DEEP (`20251212_IEXTP1_DEEP1.0.pcap.gz`) and DEEP+ (`20251212_IEXTP1_DPLS1.0.pcap.gz`). Data files stay outside the repository.

```sh
cmake --build build --target iex_replay
gzip -dc DAY.pcap.gz | build/tools/replay/iex_replay --track AAPL,MSFT,INTC --bbo-log OUT.log -
build/tools/replay/iex_replay --compare DEEP.log DEEPPLUS.log
```

Both files are pcapng inside, despite the name. The decompressor is outside the library, which keeps it free of dependencies.

## Results

| check | DEEP | DEEP+ |
|---|---|---|
| UDP datagrams, all IEX-TP segments | 314,513,830 | 335,524,535 |
| messages | 493,821,638 | 477,645,531 |
| heartbeats | 3,561 | 3,631 |
| streams | 1 | 1 |
| sequence gaps, offset mismatches, duplicates | 0 | 0 |
| bad segments, bad messages, unknown types, unknown protocols | 0 | 0 |
| timestamp regressions per symbol, send-time regressions | 0 | 0 |
| system events, in the specified order | 6, 0 out of order | 6, 0 out of order |
| trading status messages | 12,322 | 12,322 |
| symbols with book or trade activity | 11,097 | 11,097 |
| finished book events | 459,874,886 | 459,872,987 |
| crossed events, locked events | 0, 0 | 0, 0 |
| size-0 update for an absent level | 0 | not applicable |
| symbols with a non-empty book at the end | 0 | 0 |
| unknown, duplicate or oversize orders; negative levels | not applicable | 0 |

Message types, DEEP: price level buy 240,983,201, sell 243,932,650, trade 6,696,868. DEEP+: add 214,711,482, modify 40,779,869, delete 213,250,391, order executed 2,232,707, trade 4,464,161.

The two event counts differ by 1,899 because the feeds delimit events differently (Event Flags against timestamp). The books agree regardless, as the next section shows.

### The two feeds agree

DEEP+ book state is rebuilt here from individual orders and DEEP arrives as aggregated levels, so the two paths share only the framing code. For AAPL, MSFT and INTC every BBO change (timestamp, both prices, both sizes) was logged from each feed: 1,866,040 changes, and the logs are identical record for record. This is the strongest check in this document, since the second feed is independent evidence from the venue rather than our code agreeing with itself.

Across all symbols, prints agree in count (6,696,868) and shares (666,488,966); DEEP `T` equals DEEP+ `L` plus `T` exactly. Trade ids cannot be matched across feeds: DEEP numbers trades 1 to 6,696,868 with no hole (the id sum equals n(n+1)/2), while DEEP+ uses a separate range of much larger ids for both `L` and `T`. So individual prints were not paired.

### Prints against the BBO

| prints against the IEX BBO of the last finished event | count |
|---|---|
| inside or at the BBO | 6,080,372 |
| BBO one-sided or empty | 595,505 |
| outside, regular hours | 20,958 |
| outside, other sessions or crosses | 33 |

The counts are identical on both feeds, so the outside prints are not a decoding fault. They are 0.3 % of prints. The likely reason is that `T` includes non-displayed orders, which can trade at prices better than the displayed IEX book; that explanation has not been tested. Every DEEP+ displayed execution (2,232,707) hit an order at the touch of its side at its booked price.

### Throughput (indicative)

About 0.67 million messages per second on DEEP and 0.57 million on DEEP+, end to end through `gzip -dc` on one shared laptop with other builds running, under WSL reading from the Windows drive. This is bound by the pipeline, says nothing about decoder latency, and is not a benchmark.

## What this can and cannot validate for HftOrderBook

DEEP is aggregated, so on its own it cannot exercise an order book at all. DEEP+ is order by order for displayed orders, which allowed more than was first planned: for the three tracked symbols the real orders were driven through a shadow `HftOrderBook`.

- 1,155,735 orders, counting re-adds after modifies and repairs, were submitted post-only with 0 rejected, and its BBO matched the rebuilt book at all 2,030,069 events compared.
- 20,205 of 20,342 executions took the expected order at the front of the queue. The other 137 did not. 6,331 modifies kept priority at the venue but must lose it here, because `HftOrderBook` has no reduce in place (cancel and re-add is the only route), which is the likely cause; it has not been proven for each of the 137. After such a fill the shadow is repaired from the feed.

What it cannot show: the matching logic itself, since IEX's matches arrive as results and are replayed as IOC orders against a known order; non-displayed and reserve interaction; order types, pegging and price sliding; latency. Prices are held in whole cents for the shadow, and none needed skipping on this day.

## Queue estimator

On DEEP the tracked symbols ran `QueuePositionEstimator` with a notional 100-share order at the best bid: 230 joins, 2,029,845 updates, 57 reached the front, 173 lost their level, 0 invariant failures (shares ahead finite, non-negative, not above the level; progress in 0 to 1). That shows it stays sound under realistic load. It does not show its estimates are accurate; DEEP+ could supply true queue positions, and that comparison has not been done.

## Tests and fuzzing

`tests/iex_replay_test.cpp` builds its captures by hand from the public specifications, so no vendor bytes are committed. It covers both containers, a cut at every length, lying lengths, gaps, duplicates, and 8,000 randomly damaged captures. `tools/replay/fuzz_iex.cpp` is the libFuzzer target (`-DQFL_FUZZ=ON`, clang).

No bug was found in `HftOrderBook` or `QueuePositionEstimator` by this replay. The missing reduce in place is a gap in the API, not a fault.
