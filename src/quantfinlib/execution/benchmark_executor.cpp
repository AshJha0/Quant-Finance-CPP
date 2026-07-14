#include "quantfinlib/execution/benchmark_executor.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

BenchmarkExecutor::BenchmarkExecutor(Side side, std::int64_t parentQty, Benchmark benchmark,
                                     double participationRate, double alphaUrgency, double maxDepthFraction)
        : side_(side),
          parentQty_(parentQty),
          benchmark_(benchmark),
          participationRate_(participationRate),
          alphaUrgency_(alphaUrgency),
          maxDepthFraction_(maxDepthFraction) {
    if (parentQty <= 0) {
        throw std::invalid_argument("parentQty must be > 0");
    }
    if (benchmark == Benchmark::PARTICIPATION && (participationRate <= 0 || participationRate > 1)) {
        throw std::invalid_argument("PARTICIPATION needs participationRate in (0,1]");
    }
    if (alphaUrgency < 0) {
        throw std::invalid_argument("alphaUrgency must be >= 0");
    }
    if (maxDepthFraction <= 0 || maxDepthFraction > 1) {
        throw std::invalid_argument("maxDepthFraction must be in (0,1]");
    }
}

BenchmarkExecutor BenchmarkExecutor::of(Side side, std::int64_t parentQty, Benchmark benchmark) {
    if (benchmark == Benchmark::PARTICIPATION) {
        throw std::invalid_argument("PARTICIPATION needs an explicit rate: use pov(side, qty, rate)");
    }
    return BenchmarkExecutor(side, parentQty, benchmark, 0.1, 1, 0.25);
}

BenchmarkExecutor BenchmarkExecutor::pov(Side side, std::int64_t parentQty, double participationRate) {
    return BenchmarkExecutor(side, parentQty, Benchmark::PARTICIPATION, participationRate, 1, 0.25);
}

void BenchmarkExecutor::onMarketVolume(std::int64_t qty) {
    if (qty > 0) {
        marketVolume_ += qty;
    }
}

void BenchmarkExecutor::onFill(std::int64_t qty) {
    if (qty > 0) {
        executed_ += qty;
    }
}

double BenchmarkExecutor::clamp01(double x) {
    // Java's Math.min/Math.max propagate NaN (any comparison against NaN
    // is false, and both methods special-case a NaN operand to return
    // NaN); math_utils::clamp uses std::min/std::max, which instead treat
    // NaN as "not less than" and silently normalize it to the bound. VWAP's
    // NaN-volume-curve fallback in targetCompletion() depends on seeing
    // the NaN through this clamp, so it is preserved explicitly here
    // rather than by changing the shared math_utils::clamp.
    if (std::isnan(x)) {
        return x;
    }
    return math_utils::clamp(x, 0, 1);
}

double BenchmarkExecutor::targetCompletion(double f, const MarketState& m) const {
    switch (benchmark_) {
        case Benchmark::TWAP:
            return f;
        case Benchmark::VWAP:
            return clamp01(m.expectedVolumeFractionElapsed);
        case Benchmark::ARRIVAL_PRICE:
        case Benchmark::IMPLEMENTATION_SHORTFALL:
            // Front-loaded: 1-(1-f)^2 trades more early (cuts timing risk).
            return 1 - (1 - f) * (1 - f);
        case Benchmark::CLOSING_PRICE:
            // Back-loaded: f^2 keeps weight near the close.
            return f * f;
        case Benchmark::OPENING_PRICE:
            // Aggressively front-loaded: sqrt(f) is near-done early.
            return std::sqrt(f);
        case Benchmark::PARTICIPATION:
            // Volume-driven, branched off before this switch: a silent
            // fallback value here would make a mis-routed POV behave like
            // TWAP with no error -- fail loud instead.
            throw std::logic_error("PARTICIPATION is volume-driven and handled before the curve");
    }
    throw std::logic_error("unreachable benchmark");
}

double BenchmarkExecutor::urgencyMultiplier(const MarketState& m) const {
    // NaN inputs are neutral (0): a transient bad signal must weaken the
    // input, never NaN-poison the multiplier into a silent stall ((long)
    // of NaN is 0 -- "nothing due" forever).
    double alpha = std::isnan(m.alpha) ? 0 : m.alpha;
    double signedAlpha = side_ == Side::BUY ? alpha : -alpha;
    double u = 1 + alphaUrgency_ * signedAlpha;

    // Cost of trading NOW: relative spread plus the estimated impact of a
    // full child (both dimensionless fractions of price), damping
    // aggression together -- NaN in either fails the > 0 test = neutral.
    double cost = 0;
    if (m.mid > 0 && m.spread > 0) {
        cost += m.spread / m.mid;
    }
    if (m.impactBps > 0) {
        cost += m.impactBps / 1e4;
    }
    if (cost > 0) {
        u *= 1.0 / (1 + cost * SPREAD_SENSITIVITY);
    }

    // Volatility (normalized regime, ~0..1): raises urgency for
    // shortfall/arrival (timing risk), lowers it for passive benchmarks.
    if (m.volatility > 0) {
        if (benchmark_ == Benchmark::IMPLEMENTATION_SHORTFALL || benchmark_ == Benchmark::ARRIVAL_PRICE) {
            u *= 1 + m.volatility;
        } else {
            u *= 1.0 / (1 + m.volatility);
        }
    }
    return math_utils::clamp(u, 0.25, 4.0);
}

std::int64_t BenchmarkExecutor::dueQuantity(double scheduleFraction, const MarketState& m) {
    std::int64_t remaining = parentQty_ - executed_;
    if (remaining <= 0) {
        return 0;
    }
    double f = clamp01(scheduleFraction);

    std::int64_t behind;
    if (benchmark_ == Benchmark::PARTICIPATION) {
        // Volume-driven: target = participation * others' volume.
        auto target = static_cast<std::int64_t>(participationRate_ * static_cast<double>(marketVolume_));
        behind = target - executed_;
    } else {
        // Target completion by now = curve(progress) * parent. A NaN
        // volume-curve input degrades VWAP to the time fraction -- (long)
        // of NaN is 0, which would read as "nothing due" and silently
        // stall the parent.
        double targetFrac = targetCompletion(f, m);
        if (std::isnan(targetFrac)) {
            targetFrac = f;
        }
        behind = static_cast<std::int64_t>(std::lround(targetFrac * static_cast<double>(parentQty_))) - executed_;
    }
    if (behind <= 0) {
        return 0;
    }

    // Dynamic shaping: alpha pulls the pace, spread/vol damp it. For
    // PARTICIPATION the multiplier may only DAMP (clamp to <= 1): the
    // participation rate is a hard promise to the client.
    double urgency = urgencyMultiplier(m);
    if (benchmark_ == Benchmark::PARTICIPATION) {
        urgency = std::min(urgency, 1.0);
    }
    auto shaped = static_cast<std::int64_t>(std::ceil(static_cast<double>(behind) * urgency));

    // Liquidity cap and parent remainder.
    std::int64_t cap = remaining;
    if (m.displayedDepth < std::numeric_limits<double>::infinity()) {
        cap = std::min(cap, static_cast<std::int64_t>(std::floor(maxDepthFraction_ * m.displayedDepth)));
    }
    return std::max<std::int64_t>(0, std::min(shaped, cap));
}

double BenchmarkExecutor::realizedParticipation() const {
    return marketVolume_ == 0 ? std::nan("") : static_cast<double>(executed_) / static_cast<double>(marketVolume_);
}

double BenchmarkExecutor::scheduleDrift(double scheduleFraction, const MarketState& m) const {
    double executedFrac = static_cast<double>(executed_) / static_cast<double>(parentQty_);
    double target = benchmark_ == Benchmark::PARTICIPATION
            ? (marketVolume_ == 0 ? 0
                                  : std::min(1.0, participationRate_ * static_cast<double>(marketVolume_)
                                                          / static_cast<double>(parentQty_)))
            : targetCompletion(clamp01(scheduleFraction), m);
    return executedFrac - target;
}

}  // namespace quantfinlib
