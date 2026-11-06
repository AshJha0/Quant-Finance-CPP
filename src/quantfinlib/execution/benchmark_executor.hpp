// Port of com.quantfinlib.execution.BenchmarkExecutor: the dynamic
// benchmark execution algorithm -- one stateful executor that works a
// parent order toward any of the standard benchmarks (VWAP, TWAP, Arrival
// Price, Implementation Shortfall, Closing Price, Opening Price,
// Participation/POV) and, unlike a precomputed slice list, re-decides
// every interval from live market state. Cross-asset: prices/sizes are
// doubles, so it serves equities and FX identically.
//
// Two layers: (1) the benchmark curve -- each Benchmark defines the
// fraction of the parent that SHOULD be complete by now; (2) the dynamic
// adjustment -- alpha (expected short-term move), spread & volatility
// (cost/urgency trade-off), liquidity (participation cap on displayed
// depth), and fill rate / schedule drift (feed realized fills via
// onFill).
//
// DEVIATION FROM JAVA: child orders in flight count. Report each send with
// onSent, and dueQuantity excludes working quantity, so a poll between send
// and fill cannot release the same shares twice. At the end of the horizon
// (scheduleFraction >= 1) a schedule benchmark asks for everything left,
// whatever alpha or spread say. A NaN schedule fraction or a NaN or negative
// depth means "unknown", and an unknown state sends nothing.
//
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <limits>

#include "quantfinlib/orderbook/side.hpp"

namespace quantfinlib {

class BenchmarkExecutor {
public:
    /** The benchmark this parent is measured against. */
    enum class Benchmark {
        TWAP,                       // Equal participation over time.
        VWAP,                       // Follow the expected volume curve.
        ARRIVAL_PRICE,              // Minimize slippage vs arrival price (front-loaded).
        IMPLEMENTATION_SHORTFALL,   // Almgren-Chriss shortfall: front-loaded, vol raises urgency.
        CLOSING_PRICE,              // Track the closing price: back-loaded toward the close.
        OPENING_PRICE,              // Track the opening price: aggressively front-loaded.
        PARTICIPATION               // Percentage-of-volume: fixed share of realized volume.
    };

    /**
     * A snapshot of the real-time inputs a benchmark algo evaluates. A
     * neutral value disables that input: spread=0, volatility=0, alpha=0,
     * displayedDepth=+inf (no liquidity cap),
     * expectedVolumeFractionElapsed=schedule fraction (VWAP falls back to
     * TWAP), impactBps=0. NaN in a signal field is treated as its neutral
     * value. displayedDepth is a safety limit, not a signal: NaN sends nothing.
     *
     * Units contract: volatility is a normalized volatility-REGIME signal
     * ~0 calm to ~1 extreme; alpha is a normalized expected-move signal in
     * [-1, 1] (+ = up); displayedDepth is size dealable NOW.
     */
    struct MarketState {
        double mid;
        double spread;
        double volatility;
        double displayedDepth;
        double expectedVolumeFractionElapsed;
        double alpha;
        double impactBps;

        /** A neutral state: no spread/vol/alpha/impact, unlimited depth, VWAP=TWAP. */
        static MarketState neutral(double mid, double scheduleFraction) {
            return MarketState{mid, 0, 0, std::numeric_limits<double>::infinity(), scheduleFraction, 0, 0};
        }
    };

    /**
     * @param side              buy or sell (sets the alpha sign convention)
     * @param parentQty         total quantity to execute
     * @param benchmark         the benchmark to track
     * @param participationRate POV target in (0,1] (ignored unless PARTICIPATION)
     * @param alphaUrgency      how hard alpha shifts the pace (0 disables; ~5-20 typical)
     * @param maxDepthFraction  cap each child at this fraction of displayed depth (0,1]
     */
    BenchmarkExecutor(Side side, std::int64_t parentQty, Benchmark benchmark, double participationRate,
                      double alphaUrgency, double maxDepthFraction);

    /**
     * Sensible defaults: alpha urgency 1 (a full-scale normalized alpha of
     * +/-1 doubles/halves the pace), child capped at 25% of displayed
     * depth. PARTICIPATION must state its rate -- use pov().
     */
    static BenchmarkExecutor of(Side side, std::int64_t parentQty, Benchmark benchmark);

    /** POV convenience. */
    static BenchmarkExecutor pov(Side side, std::int64_t parentQty, double participationRate);

    // -------------------------------------------------------------- Feed

    /** A market print that was NOT our fill (drives VWAP/POV realized volume). */
    void onMarketVolume(std::int64_t qty);

    /** Our own child fill; releases the same quantity of working orders. */
    void onFill(std::int64_t qty);

    /** A child order went out. Throws if it would overfill the parent. */
    void onSent(std::int64_t qty);

    /** Working quantity came back unfilled (cancel, reject, expiry). */
    void onCancelled(std::int64_t qty);

    /**
     * Round every due quantity down to whole lots. A remainder below one
     * lot is released as a final odd lot once nothing else is due.
     */
    BenchmarkExecutor& withLotSize(std::int64_t lotSize);

    // -------------------------------------------------------------- Decision

    /** A deliberate calibration constant, not a unit conversion. */
    static constexpr double SPREAD_SENSITIVITY = 100;

    /**
     * Shares to send now, given the current market and how far through the
     * schedule we are, net of quantity already working. Returns 0 when
     * on/ahead of schedule or done; caps at the unsent parent remainder and
     * at maxDepthFraction * displayedDepth. Polling does not reserve: call
     * onSent for what is actually sent.
     *
     * @param scheduleFraction elapsed fraction of the execution horizon in
     *                         [0, 1]; ignored by PARTICIPATION
     * @param m                the live market snapshot
     */
    std::int64_t dueQuantity(double scheduleFraction, const MarketState& m);

    // -------------------------------------------------------------- Progress

    std::int64_t executed() const { return executed_; }
    std::int64_t remaining() const { return parentQty_ - executed_; }
    std::int64_t working() const { return working_; }
    std::int64_t parentQty() const { return parentQty_; }
    std::int64_t marketVolume() const { return marketVolume_; }
    bool done() const { return executed_ >= parentQty_; }

    /** Realized participation vs other-flow volume (NaN before any market print). */
    double realizedParticipation() const;

    /**
     * Schedule drift: executed fraction minus the benchmark's target
     * fraction at scheduleFraction. Positive = ahead, negative = behind.
     */
    double scheduleDrift(double scheduleFraction, const MarketState& m) const;

    Side side() const { return side_; }
    Benchmark benchmark() const { return benchmark_; }

private:
    double targetCompletion(double f, const MarketState& m) const;
    double urgencyMultiplier(const MarketState& m) const;
    static double clamp01(double x);

    Side side_;
    std::int64_t parentQty_;
    Benchmark benchmark_;
    double participationRate_;
    double alphaUrgency_;
    double maxDepthFraction_;

    std::int64_t executed_ = 0;
    std::int64_t working_ = 0;
    std::int64_t marketVolume_ = 0;
    std::int64_t lotSize_ = 1;
};

}  // namespace quantfinlib
