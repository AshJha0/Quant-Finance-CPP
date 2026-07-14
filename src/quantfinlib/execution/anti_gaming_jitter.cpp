#include "quantfinlib/execution/anti_gaming_jitter.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {
/** Java's Math.round(double): floor(x + 0.5), NOT round-half-away-from-zero. */
std::int64_t javaRound(double x) {
    return static_cast<std::int64_t>(std::floor(x + 0.5));
}
}  // namespace

AntiGamingJitter::AntiGamingJitter(std::int64_t seed, double sizeFraction, double timeFraction)
        : rnd_(seed), sizeFraction_(sizeFraction), timeFraction_(timeFraction) {
    if (!(sizeFraction >= 0 && sizeFraction <= 0.5)) {
        throw std::invalid_argument("sizeFraction must be in [0, 0.5]");
    }
    if (!(timeFraction >= 0 && timeFraction <= 0.5)) {
        throw std::invalid_argument("timeFraction must be in [0, 0.5]");
    }
}

std::vector<std::int64_t> AntiGamingJitter::jitterSizes(const std::vector<std::int64_t>& childQty) {
    std::vector<std::int64_t> out = childQty;
    for (std::int64_t q : out) {
        if (q < 0) {
            throw std::invalid_argument("child quantities must be >= 0");
        }
    }
    for (std::size_t i = 0; i + 1 < out.size(); i++) {
        // Transfer between neighbors: total invariant by construction.
        auto maxShift = static_cast<std::int64_t>(
                std::floor(static_cast<double>(std::min(out[i], out[i + 1])) * sizeFraction_));
        if (maxShift <= 0) {
            continue;
        }
        std::int64_t shift = javaRound((2 * rnd_.nextDouble() - 1) * static_cast<double>(maxShift));
        out[i] += shift;
        out[i + 1] -= shift;
    }
    return out;
}

std::vector<std::int64_t> AntiGamingJitter::jitterTimes(const std::vector<std::int64_t>& timesNanos,
                                                        std::int64_t startNanos) {
    std::vector<std::int64_t> out = timesNanos;
    for (std::size_t i = 1; i < out.size(); i++) {
        if (out[i] <= out[i - 1]) {
            throw std::invalid_argument("times must be strictly increasing");
        }
    }
    if (!out.empty() && out[0] < startNanos) {
        throw std::invalid_argument("first time is before start");
    }
    for (std::size_t i = 0; i < out.size(); i++) {
        std::int64_t lo = i == 0 ? startNanos : out[i - 1] + 1;
        std::int64_t hi = i == out.size() - 1 ? timesNanos.back()
                                              : out[i + 1] - 1;  // next entry not yet jittered: safe bound
        std::int64_t gapBefore = timesNanos[i] - (i == 0 ? startNanos : timesNanos[i - 1]);
        auto maxShift = static_cast<std::int64_t>(std::floor(static_cast<double>(gapBefore) * timeFraction_));
        std::int64_t shifted =
                timesNanos[i] + javaRound((2 * rnd_.nextDouble() - 1) * static_cast<double>(maxShift));
        out[i] = std::max(lo, std::min(hi, shifted));
    }
    return out;
}

}  // namespace quantfinlib
