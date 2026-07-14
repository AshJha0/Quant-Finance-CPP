// Port of com.quantfinlib.trading.GlobalRiskAggregator (Java): firm-wide
// risk across shards -- the piece sharding deliberately doesn't solve: each
// shard's HftRiskGate sees only its own symbols, so a "total gross notional
// across the firm" cap needs someone who can see all of them.
//
//  - a monitor thread polls every gate's positions and reference prices
//    (both already published with release/acquire semantics);
//  - gross notional = sum |position| x referencePrice over all gates and
//    symbols (symbols without a reference price contribute zero);
//  - breach -> HftRiskGate::kill on EVERY gate: one released boolean, read
//    by each shard's checks as a single acquire load. Recovery is
//    hysteretic: trading resumes only below cap x resumeFraction, so the
//    firm doesn't flap around the limit.
//
// The detection latency is the poll interval (default 1 ms) -- a
// deliberate trade: pre-trade per-order checks stay per-shard and
// nanosecond-cheap, while the firm-wide cap is a circuit breaker, not a
// per-order gate.
#pragma once

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

#include "quantfinlib/trading/hft_risk_gate.hpp"

namespace quantfinlib {

class GlobalRiskAggregator {
public:
    /**
     * @param gates             every shard's gate (see ShardedTradingEngine::gates())
     * @param maxGrossNotional  firm-wide cap on sum |position| x referencePrice
     * @param resumeFraction    resume trading below cap x this (e.g. 0.9)
     * @param pollIntervalNanos monitor cadence (1'000'000 = 1 ms detection)
     * @throws std::invalid_argument if gates is empty, cap <= 0, resumeFraction
     *         not in (0, 1), or pollIntervalNanos <= 0
     */
    GlobalRiskAggregator(std::vector<HftRiskGate*> gates, double maxGrossNotional,
                         double resumeFraction, std::int64_t pollIntervalNanos);

    ~GlobalRiskAggregator() { close(); }

    GlobalRiskAggregator(const GlobalRiskAggregator&) = delete;
    GlobalRiskAggregator& operator=(const GlobalRiskAggregator&) = delete;

    /** One sweep over every gate's positions -- acquire reads only. */
    double grossNotional() const;

    /** Whether the firm-wide breaker is currently tripped. */
    bool isTripped() const { return tripped_.load(std::memory_order_acquire); }

    /** Gross notional from the most recent monitor sweep. */
    double lastGrossNotional() const { return lastGross_.load(std::memory_order_acquire); }

    /** Times the breaker has tripped over this aggregator's life. */
    std::int64_t tripCount() const { return trips_.load(std::memory_order_acquire); }

    /** Stops the monitor; gates keep whatever kill state they last had. */
    void close();

private:
    void monitorLoop();

    std::vector<HftRiskGate*> gates_;
    const double maxGrossNotional_;
    const double resumeNotional_;
    const std::int64_t pollIntervalNanos_;
    std::atomic<bool> running_{true};
    std::atomic<bool> tripped_{false};
    std::atomic<double> lastGross_{0.0};
    std::atomic<std::int64_t> trips_{0};
    std::vector<bool> killedBeforeTrip_;   // monitor thread only
    std::thread monitor_;
};

}  // namespace quantfinlib
