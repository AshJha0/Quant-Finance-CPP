# Java and C++ side by side

Measured 2026-11-06. Both languages ran on one machine in one OS, alternating for three rounds, so drift hits both equally.

**Setup.** Intel Core 5 120U (hybrid cores), WSL2 Linux 6.18, unpinned. C++: this tree, GCC 15.2, Release. Java: Quant-Finance-Library 4.0.0 on OpenJDK 25.0.5, default flags, using its own `HftBookBenchmark` and `HftOrderBenchmark`.

**Read this first.** These numbers are indicative. WSL2 vCPUs float over physical cores, so tails measure the host and are not compared. The two benchmark suites were written separately and **do not run the same workload**, so no row below is a clean language-against-language result, and no speed-up ratio should be quoted from it.

| Stage | Java | C++ | Why they differ beyond language |
|---|---|---|---|
| Risk gate, throughput per check (mean) | 11.5 ns | 3.7 ns | Java spreads checks over 1024 random symbols and rejects some; C++ uses a hot batch |
| Cross-thread hand-off (p50) | 277 ns | 132 ns | Java is submit-to-venue and includes the gate; C++ is the ring alone |
| Market data to consumer (p50) | 453 ns | 148 ns | Java goes tick, strategy, gate, then venue thread; C++ stops at the bus listener |
| Book operation (p50) | 94 ns | 35 add, 43 cancel | Java is per op within 128-op mixed batches with heavy matching; C++ is per op on a 16k book |

Java values are the middle of three rounds, and its reported percentiles are upper bounds of histogram buckets. C++ fenced timings carry a timer floor of about 46 ns that is not subtracted, and some reported per-op values sit below it, so values near the floor are not resolved.

**What this supports.** On this machine C++ was faster on each of these rows, and Java's p50 to p99 range stayed tight once warmed. It does not support a specific multiple.

**What would make it a real comparison.** One shared workload definition implemented in both languages, on a pinned bare-metal Linux host. No such host is available, so that work is not done.

The comparisons published for v1.0.0 were wrong to call this the same machine: their Java figures were hard-coded from a separate run.
