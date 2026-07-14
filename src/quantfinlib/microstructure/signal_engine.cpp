#include "quantfinlib/microstructure/signal_engine.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/pricing/fair_value_engine.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

SignalEngine::SignalEngine(int symbolCount, Config config)
        : config_(config),
          symbolCount_(symbolCount),
          sqrtFastHorizonSec_(std::sqrt(static_cast<double>(config.fastHalfLifeNanos) * 1e-9)),
          weightSum_(config.wQueueImbalance + config.wTradeImbalance + config.wOfi
                     + config.wMomentum),
          microprice_(static_cast<std::size_t>(symbolCount),
                      std::numeric_limits<double>::quiet_NaN()),
          prevMid_(static_cast<std::size_t>(symbolCount)),
          prevMidTime_(static_cast<std::size_t>(symbolCount)),
          fastEma_(static_cast<std::size_t>(symbolCount)),
          slowEma_(static_cast<std::size_t>(symbolCount)),
          varRatePerSec_(static_cast<std::size_t>(symbolCount)),
          spreadEwma_(static_cast<std::size_t>(symbolCount)),
          depthEwma_(static_cast<std::size_t>(symbolCount)),
          gapEwmaNanos_(static_cast<std::size_t>(symbolCount)),
          hasMid_(static_cast<std::size_t>(symbolCount), false) {
    if (symbolCount < 1) {
        throw std::invalid_argument("symbolCount must be >= 1");
    }
    flow_.reserve(static_cast<std::size_t>(symbolCount));
    for (int s = 0; s < symbolCount; s++) {
        flow_.emplace_back(config.flowHalfLifeNanos);
    }
}

void SignalEngine::onQuote(int symbolId, double bid, std::int64_t bidSize, double ask,
                           std::int64_t askSize, std::int64_t timestampNanos) {
    quoteCount_++;
    auto i = static_cast<std::size_t>(symbolId);
    flow_[i].onQuote(bid, bidSize, ask, askSize, timestampNanos);
    // Gap gate: sizes must be positive and prices DEALABLE (strictly
    // positive and finite) -- !(x > 0) also catches NaN.
    if (bidSize <= 0 || askSize <= 0 || !(bid > 0) || !(ask > 0)
            || bid == std::numeric_limits<double>::infinity()
            || ask == std::numeric_limits<double>::infinity()) {
        hasMid_[i] = false;  // gap: next two-sided quote re-seeds
        return;
    }
    double mid = 0.5 * (bid + ask);
    microprice_[i] = FairValueEngine::microprice(bid, ask, static_cast<double>(bidSize),
                                                  static_cast<double>(askSize));

    if (hasMid_[i]) {
        std::int64_t dt = timestampNanos - prevMidTime_[i];
        if (dt > 0) {
            double dtSec = static_cast<double>(dt) * 1e-9;
            double aFast = 1 - math_utils::decayFactor(dt, config_.fastHalfLifeNanos);
            double aSlow = 1 - math_utils::decayFactor(dt, config_.slowHalfLifeNanos);
            fastEma_[i] += aFast * (mid - fastEma_[i]);
            slowEma_[i] += aSlow * (mid - slowEma_[i]);
            double r = (mid - prevMid_[i]) / prevMid_[i];
            double aVol = 1 - math_utils::decayFactor(dt, config_.volHalfLifeNanos);
            varRatePerSec_[i] += aVol * (r * r / dtSec - varRatePerSec_[i]);
            double aLiq = 1 - math_utils::decayFactor(dt, config_.liquidityHalfLifeNanos);
            spreadEwma_[i] += aLiq * ((ask - bid) - spreadEwma_[i]);
            depthEwma_[i] += aLiq * (static_cast<double>(bidSize + askSize) - depthEwma_[i]);
            gapEwmaNanos_[i] += aLiq * (static_cast<double>(dt) - gapEwmaNanos_[i]);
            prevMid_[i] = mid;
            prevMidTime_[i] = timestampNanos;
        }
        // dt <= 0 (same-timestamp burst): fold into the same observation
        // window -- mid/microprice refresh above, estimators wait.
    } else {
        // Seed all baselines from the first two-sided quote after a gap.
        hasMid_[i] = true;
        prevMid_[i] = mid;
        prevMidTime_[i] = timestampNanos;
        fastEma_[i] = mid;
        slowEma_[i] = mid;
        spreadEwma_[i] = ask - bid;
        depthEwma_[i] = static_cast<double>(bidSize + askSize);
        varRatePerSec_[i] = 0;
        gapEwmaNanos_[i] = 0;
    }
}

void SignalEngine::onTrade(int symbolId, bool buyAggressor, std::int64_t quantity,
                           std::int64_t timestampNanos) {
    tradeCount_++;
    flow_[static_cast<std::size_t>(symbolId)].onTrade(buyAggressor, quantity, timestampNanos);
}

double SignalEngine::volPerSqrtSecond(int symbolId) const {
    double v = varRatePerSec_[static_cast<std::size_t>(symbolId)];
    return std::sqrt(v > 0 ? v : 0);
}

double SignalEngine::spreadBps(int symbolId) const {
    auto i = static_cast<std::size_t>(symbolId);
    double mid = prevMid_[i];
    return hasMid_[i] && mid > 0 ? spreadEwma_[i] / mid * 1e4
                                  : std::numeric_limits<double>::quiet_NaN();
}

double SignalEngine::quoteIntensityPerSecond(int symbolId) const {
    double gap = gapEwmaNanos_[static_cast<std::size_t>(symbolId)];
    return gap > 0 ? 1e9 / gap : 0;
}

double SignalEngine::momentum(int symbolId) const {
    auto i = static_cast<std::size_t>(symbolId);
    if (!hasMid_[i] || prevMid_[i] <= 0) {
        return 0;
    }
    return (fastEma_[i] - slowEma_[i]) / prevMid_[i];
}

double SignalEngine::alpha(int symbolId) const {
    if (weightSum_ <= 0) {
        return 0;
    }
    double score = config_.wQueueImbalance * queueImbalance(symbolId)
                   + config_.wTradeImbalance * tradeImbalance(symbolId)
                   + config_.wOfi * normalizedOfi(symbolId)
                   + config_.wMomentum * momentumZ(symbolId);
    return score / weightSum_;
}

double SignalEngine::normalizedOfi(int symbolId) const {
    double depth = depthEwma_[static_cast<std::size_t>(symbolId)];
    if (depth <= 0) {
        return 0;
    }
    return math_utils::clamp(ofi(symbolId) / depth, -1, 1);
}

double SignalEngine::momentumZ(int symbolId) const {
    double vol = volPerSqrtSecond(symbolId);
    if (vol <= 0) {
        return 0;
    }
    return math_utils::clamp(momentum(symbolId) / (vol * sqrtFastHorizonSec_), -1, 1);
}

}  // namespace quantfinlib
