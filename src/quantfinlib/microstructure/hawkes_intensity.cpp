#include "quantfinlib/microstructure/hawkes_intensity.hpp"

#include <cmath>
#include <sstream>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

HawkesIntensity::HawkesIntensity(double baselineRatePerSec, double excitation,
                                 std::int64_t decayHalfLifeNanos)
        : baselineRatePerSec_(baselineRatePerSec), excitation_(excitation),
          decayHalfLifeNanos_(decayHalfLifeNanos) {
    if (baselineRatePerSec <= 0 || excitation < 0 || decayHalfLifeNanos <= 0) {
        throw std::invalid_argument("need baselineRate > 0, excitation >= 0, halfLife > 0");
    }
    double beta = std::log(2.0) / (static_cast<double>(decayHalfLifeNanos) * 1e-9);
    if (excitation / beta >= 1) {
        std::ostringstream oss;
        oss << "explosive: branching ratio " << (excitation / beta)
            << " >= 1 (each event spawns >= 1 child)";
        throw std::invalid_argument(oss.str());
    }
}

void HawkesIntensity::onEvent(std::int64_t timestampNanos) {
    if (hasEvent_) {
        std::int64_t dt = timestampNanos - lastEventNanos_;
        if (dt < 0) {
            return;
        }
        excited_ = excited_ * math_utils::decayFactor(dt, decayHalfLifeNanos_) + excitation_;
    } else {
        excited_ = excitation_;
        hasEvent_ = true;
    }
    lastEventNanos_ = timestampNanos;
    events_++;
}

double HawkesIntensity::intensity(std::int64_t nowNanos) const {
    return baselineRatePerSec_ + excitedAt(nowNanos);
}

double HawkesIntensity::burstScore(std::int64_t nowNanos) const {
    return math_utils::clamp(excitedAt(nowNanos) / baselineRatePerSec_, 0, 1);
}

double HawkesIntensity::excitedAt(std::int64_t nowNanos) const {
    if (!hasEvent_) {
        return 0;
    }
    std::int64_t dt = nowNanos - lastEventNanos_;
    return dt <= 0 ? excited_ : excited_ * math_utils::decayFactor(dt, decayHalfLifeNanos_);
}

}  // namespace quantfinlib
