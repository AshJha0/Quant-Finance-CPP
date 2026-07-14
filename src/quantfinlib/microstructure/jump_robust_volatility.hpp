// Port of com.quantfinlib.microstructure.JumpRobustVolatility: bipower
// variation (Barndorff-Nielsen & Shephard 2004) streaming volatility --
// uses the product of CONSECUTIVE absolute returns, (pi/2)*|r_t|*|r_t-1|,
// normalized by sqrt(dt_t * dt_t-1) (each |r| carries sqrt of its OWN
// interval), rather than r^2, so a single jump inflates only one factor
// of two neighboring products instead of one whole squared term. A
// non-finite return or non-positive dt drops the sample AND breaks the
// consecutive-return pairing.
#pragma once

#include <cstdint>

namespace quantfinlib {

class JumpRobustVolatility {
public:
    /** @param halfLifeNanos decay half-life, e.g. 10s. */
    explicit JumpRobustVolatility(std::int64_t halfLifeNanos = 10'000'000'000LL);

    /** One return observation: relative mid change over the elapsed dtNanos. */
    void onReturn(double ret, std::int64_t dtNanos);

    /** The jump-robust (bipower) volatility, as return per sqrt(second). */
    double volPerSqrtSecond() const;

    /** The plain squared-return volatility (jumps and all), per sqrt(second). */
    double rawVolPerSqrtSecond() const;

    /** Share of raw variance attributed to jumps: clamp(1 - bipower/raw, 0, 1). */
    double jumpFraction() const;

    std::int64_t samples() const { return samples_; }

private:
    std::int64_t halfLifeNanos_;

    double rawRatePerSec_ = 0;
    double bipowerRatePerSec_ = 0;
    double prevAbsReturn_ = 0;
    double prevDtSec_ = 0;
    bool hasPrev_ = false;
    bool bipowerSeeded_ = false;
    std::int64_t samples_ = 0;
};

}  // namespace quantfinlib
