#include "quantfinlib/microstructure/jump_robust_volatility.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {
constexpr double kHalfPi = 3.14159265358979323846 / 2;
}

JumpRobustVolatility::JumpRobustVolatility(std::int64_t halfLifeNanos)
        : halfLifeNanos_(halfLifeNanos) {
    if (halfLifeNanos <= 0) {
        throw std::invalid_argument("halfLifeNanos must be positive");
    }
}

void JumpRobustVolatility::onReturn(double ret, std::int64_t dtNanos) {
    if (!std::isfinite(ret) || dtNanos <= 0) {
        hasPrev_ = false;
        return;
    }
    double dtSec = static_cast<double>(dtNanos) * 1e-9;
    double a = 1 - math_utils::decayFactor(dtNanos, halfLifeNanos_);

    double rawObs = ret * ret / dtSec;
    rawRatePerSec_ = samples_ == 0 ? rawObs : rawRatePerSec_ + a * (rawObs - rawRatePerSec_);

    double absRet = std::abs(ret);
    if (hasPrev_) {
        double bpObs = kHalfPi * absRet * prevAbsReturn_ / std::sqrt(dtSec * prevDtSec_);
        bipowerRatePerSec_ = bipowerSeeded_
                ? bipowerRatePerSec_ + a * (bpObs - bipowerRatePerSec_)
                : bpObs;
        bipowerSeeded_ = true;
    }
    prevAbsReturn_ = absRet;
    prevDtSec_ = dtSec;
    hasPrev_ = true;
    samples_++;
}

double JumpRobustVolatility::volPerSqrtSecond() const {
    return std::sqrt(std::max(bipowerRatePerSec_, 0.0));
}

double JumpRobustVolatility::rawVolPerSqrtSecond() const {
    return std::sqrt(std::max(rawRatePerSec_, 0.0));
}

double JumpRobustVolatility::jumpFraction() const {
    if (rawRatePerSec_ <= 0 || !bipowerSeeded_) {
        return 0;
    }
    return math_utils::clamp(1 - bipowerRatePerSec_ / rawRatePerSec_, 0, 1);
}

}  // namespace quantfinlib
