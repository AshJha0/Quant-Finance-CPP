// Port of com.quantfinlib.execution.LiquiditySeekingAlgo: the
// opportunistic execution archetype -- the counterpart to
// BenchmarkExecutor's schedule-driven family. A schedule algo asks "am I
// behind the curve?"; a liquidity seeker asks "is the market cheap RIGHT
// NOW?" and trades in bursts when it is: spread tighter than its
// time-of-day forecast, calm volatility regime, low estimated impact.
// Between opportunities it sits still -- which is why it needs a
// completion floor that ramps in over the final stretch of the horizon,
// so patience can never become a missed parent.
//
// Decision per interval: score the moment -- spread at-or-under forecast,
// vol regime below the calm threshold, impact below the cap -- and if ALL
// hold, take an aggressive clip (maxDepthFraction * displayedDepth). If
// not, send only the completion floor: 0 until forceCompleteFrom, then the
// remaining quantity spread linearly over what is left of the horizon (at
// f = 1 the floor IS the remainder). NaN inputs degrade honestly: an
// unknown forecast means the moment cannot be judged cheap, but the floor
// still guarantees completion. Zero allocation per decision, single
// parent, single writer. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

#include <cstdint>

#include "quantfinlib/execution/benchmark_executor.hpp"

namespace quantfinlib {

class LiquiditySeekingAlgo {
public:
    /**
     * @param spreadTolerance   how far above forecast still counts as
     *                          cheap, as a fraction (0.1 = up to 10% over)
     * @param maxVolRegime      opportunistic bursts only below this
     *                          normalized vol regime (0..1), e.g. 0.5
     * @param maxImpactBps      opportunistic bursts only below this
     *                          estimated impact, e.g. 5 bps
     * @param maxDepthFraction  burst clip as a fraction of displayed
     *                          depth, in (0, 1]
     * @param forceCompleteFrom schedule fraction where the completion
     *                          floor starts ramping, in [0, 1), e.g. 0.7
     */
    struct Config {
        double spreadTolerance;
        double maxVolRegime;
        double maxImpactBps;
        double maxDepthFraction;
        double forceCompleteFrom;

        Config(double spreadTolerance_, double maxVolRegime_, double maxImpactBps_,
              double maxDepthFraction_, double forceCompleteFrom_);

        /** 10% spread tolerance, vol regime < 0.5, impact < 5 bps, 25% clips, floor from 70%. */
        static Config defaults();
    };

    LiquiditySeekingAlgo(std::int64_t parentQty, const Config& config);
    explicit LiquiditySeekingAlgo(std::int64_t parentQty);

    /**
     * Shares to send now. An opportunistic burst when the moment is cheap;
     * otherwise the completion floor. Always capped at the remainder.
     *
     * @param scheduleFraction elapsed fraction of the horizon in [0, 1]
     * @param m                the live market snapshot
     * @param forecastSpread   the expected spread for this time of day;
     *                         NaN = cheapness unknowable, floor only
     */
    std::int64_t dueQuantity(double scheduleFraction, const BenchmarkExecutor::MarketState& m,
                             double forecastSpread);

    /** Our own child fill. */
    void onFill(std::int64_t qty);

    std::int64_t executed() const { return executed_; }
    std::int64_t remaining() const { return parentQty_ - executed_; }
    bool done() const { return executed_ >= parentQty_; }

private:
    bool isCheap(const BenchmarkExecutor::MarketState& m, double forecastSpread) const;

    std::int64_t parentQty_;
    Config config_;
    std::int64_t executed_ = 0;
};

}  // namespace quantfinlib
