// Port of com.quantfinlib.microstructure.SignalEngine (Java): the unified
// streaming signal engine -- one multi-symbol, hot-lane component that
// turns raw top-of-book quotes and trade prints into five signal families:
//
//  - Imbalance   -- order-flow imbalance (Cont-Kukanov-Stoikov, time-decayed),
//                   inside queue imbalance and signed trade-flow imbalance,
//                   via a per-symbol FlowSignals;
//  - Fair value  -- the size-weighted microprice (FairValueEngine::microprice);
//  - Volatility  -- a streaming EWMA realized-variance rate over irregular
//                   tick arrivals: per valid mid change, r^2/dt enters a
//                   time-decayed average, and volPerSqrtSecond is its
//                   square root;
//  - Liquidity   -- time-decayed EWMAs of the absolute spread (and spreadBps
//                   of mid), displayed top-of-book depth, and quote arrival
//                   intensity (from the decayed inter-quote gap);
//  - Momentum    -- two time-aware EMAs of the mid (decay by elapsed time,
//                   not by update count), read as the normalized fast/slow
//                   gap (fast - slow)/mid.
//
// Composite alpha() blends the dimensionless forms under caller-set
// weights -- a research scaffold, not a validated alpha.
//
// Gap discipline (inherited from FlowSignals, applied to every family): a
// one-sided or NaN quote is a signal gap -- nothing updates, nothing
// poisons, and the next two-sided quote re-seeds baselines. Zero
// allocation per event after construction; single writer; dense int
// symbol ids, exactly like the market-data bus.
#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "quantfinlib/microstructure/flow_signals.hpp"

namespace quantfinlib {

class SignalEngine {
public:
    /**
     * Half-lives (all in nanos) for the decayed estimators, plus the
     * composite weights. defaults() is a reasonable intraday starting
     * point -- 500ms flow, 2s/20s momentum, 10s volatility and liquidity,
     * equal composite weights.
     */
    struct Config {
        std::int64_t flowHalfLifeNanos;
        std::int64_t fastHalfLifeNanos;
        std::int64_t slowHalfLifeNanos;
        std::int64_t volHalfLifeNanos;
        std::int64_t liquidityHalfLifeNanos;
        double wQueueImbalance;
        double wTradeImbalance;
        double wOfi;
        double wMomentum;

        Config(std::int64_t flowHalfLifeNanos_, std::int64_t fastHalfLifeNanos_,
               std::int64_t slowHalfLifeNanos_, std::int64_t volHalfLifeNanos_,
               std::int64_t liquidityHalfLifeNanos_, double wQueueImbalance_,
               double wTradeImbalance_, double wOfi_, double wMomentum_)
                : flowHalfLifeNanos(flowHalfLifeNanos_),
                  fastHalfLifeNanos(fastHalfLifeNanos_),
                  slowHalfLifeNanos(slowHalfLifeNanos_),
                  volHalfLifeNanos(volHalfLifeNanos_),
                  liquidityHalfLifeNanos(liquidityHalfLifeNanos_),
                  wQueueImbalance(wQueueImbalance_),
                  wTradeImbalance(wTradeImbalance_),
                  wOfi(wOfi_),
                  wMomentum(wMomentum_) {
            if (flowHalfLifeNanos <= 0 || fastHalfLifeNanos <= 0 || slowHalfLifeNanos <= 0
                    || volHalfLifeNanos <= 0 || liquidityHalfLifeNanos <= 0) {
                throw std::invalid_argument("half-lives must be positive");
            }
            if (fastHalfLifeNanos >= slowHalfLifeNanos) {
                throw std::invalid_argument("momentum needs fastHalfLife < slowHalfLife");
            }
            if (wQueueImbalance < 0 || wTradeImbalance < 0 || wOfi < 0 || wMomentum < 0) {
                throw std::invalid_argument("composite weights must be >= 0");
            }
        }

        static Config defaults() {
            return Config(500'000'000LL, 2'000'000'000LL, 20'000'000'000LL, 10'000'000'000LL,
                          10'000'000'000LL, 1, 1, 1, 1);
        }
    };

    explicit SignalEngine(int symbolCount) : SignalEngine(symbolCount, Config::defaults()) {}

    SignalEngine(int symbolCount, Config config);

    // ------------------------------------------------------------------
    // Feed (the hot path)
    // ------------------------------------------------------------------

    /**
     * Top-of-book update. Prices are raw doubles (FX rates directly; for
     * equities pass ticks or converted prices). One-sided/NaN quotes are
     * gaps: counted, but no estimator updates and no baseline poisoning.
     */
    void onQuote(int symbolId, double bid, std::int64_t bidSize, double ask,
                std::int64_t askSize, std::int64_t timestampNanos);

    /** Trade print with aggressor side (Lee-Ready if the venue doesn't say). */
    void onTrade(int symbolId, bool buyAggressor, std::int64_t quantity,
                std::int64_t timestampNanos);

    // ------------------------------------------------------------------
    // Imbalance
    // ------------------------------------------------------------------

    /** Time-decayed net order-flow imbalance, in size units (+ = buying pressure). */
    double ofi(int symbolId) const { return flow_[static_cast<std::size_t>(symbolId)].ofi(); }

    /** Inside queue imbalance in [-1, 1]; 0 on a one-sided book. */
    double queueImbalance(int symbolId) const {
        return flow_[static_cast<std::size_t>(symbolId)].queueImbalance();
    }

    /** Signed aggressor-volume imbalance in [-1, 1]; 0 before any trade. */
    double tradeImbalance(int symbolId) const {
        return flow_[static_cast<std::size_t>(symbolId)].tradeImbalance();
    }

    // ------------------------------------------------------------------
    // Fair value / volatility / liquidity / momentum
    // ------------------------------------------------------------------

    /** Size-weighted microprice; NaN before the first two-sided quote. */
    double microprice(int symbolId) const { return microprice_[static_cast<std::size_t>(symbolId)]; }

    /**
     * Streaming realized volatility as return per sqrt(second) (0 until two
     * valid mids).
     */
    double volPerSqrtSecond(int symbolId) const;

    /** Time-decayed absolute inside spread (0 until seeded). */
    double spread(int symbolId) const { return spreadEwma_[static_cast<std::size_t>(symbolId)]; }

    /** Decayed spread as basis points of the current mid; NaN before a mid. */
    double spreadBps(int symbolId) const;

    /** Time-decayed displayed depth (bid + ask size at the inside). */
    double topDepth(int symbolId) const { return depthEwma_[static_cast<std::size_t>(symbolId)]; }

    /** Quote arrival intensity, per second (0 until two quotes). */
    double quoteIntensityPerSecond(int symbolId) const;

    /**
     * Normalized momentum: (fast EMA - slow EMA) / mid. Positive = the
     * short horizon trades above the long horizon. 0 until seeded.
     */
    double momentum(int symbolId) const;

    // ------------------------------------------------------------------
    // Composite
    // ------------------------------------------------------------------

    /**
     * The weighted intraday composite in roughly [-1, 1]. 0 when unseeded
     * or all weights are 0. A research scaffold.
     */
    double alpha(int symbolId) const;

    /** OFI as a fraction of decayed displayed depth, clamped to [-1, 1]. */
    double normalizedOfi(int symbolId) const;

    /**
     * Momentum scaled by the volatility accrued over the fast horizon --
     * a t-statistic-like ratio -- clamped to [-1, 1].
     */
    double momentumZ(int symbolId) const;

    int symbolCount() const { return symbolCount_; }

    std::int64_t quoteCount() const { return quoteCount_; }

    std::int64_t tradeCount() const { return tradeCount_; }

private:
    Config config_;
    int symbolCount_;
    std::vector<FlowSignals> flow_;
    double sqrtFastHorizonSec_;
    double weightSum_;

    std::vector<double> microprice_;
    std::vector<double> prevMid_;
    std::vector<std::int64_t> prevMidTime_;
    std::vector<double> fastEma_;
    std::vector<double> slowEma_;
    std::vector<double> varRatePerSec_;
    std::vector<double> spreadEwma_;
    std::vector<double> depthEwma_;
    std::vector<double> gapEwmaNanos_;
    std::vector<bool> hasMid_;

    std::int64_t quoteCount_ = 0;
    std::int64_t tradeCount_ = 0;
};

}  // namespace quantfinlib
