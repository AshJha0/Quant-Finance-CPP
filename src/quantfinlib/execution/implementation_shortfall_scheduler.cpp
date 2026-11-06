#include "quantfinlib/execution/implementation_shortfall_scheduler.hpp"

#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "quantfinlib/execution/vwap_scheduler.hpp"

namespace quantfinlib {
namespace implementation_shortfall_scheduler {

namespace {

/** First-slice fraction, NaN mapped to +inf so overflow reads as "too front-loaded". */
double frontLoadOf(const AlmgrenChriss::Params& base, double lambda) {
    AlmgrenChriss::Trajectory t = AlmgrenChriss::optimalTrajectory(base.withRiskAversion(lambda));
    double f = t.trades[0] / base.totalShares;
    return std::isnan(f) ? std::numeric_limits<double>::infinity() : f;
}

}  // namespace

std::vector<Slice> schedule(const AlmgrenChriss::Params& params, std::int64_t durationMillis) {
    AlmgrenChriss::Trajectory t = AlmgrenChriss::optimalTrajectory(params);
    const std::vector<double>& weights = t.trades;
    for (double w : weights) {
        if (!std::isfinite(w)) {
            // sinh overflows around kappa*T ~ 710; without this guard the
            // NaN weights would silently degrade the integer allocation
            // into an O(parentShares) loop yielding a garbage schedule.
            throw std::invalid_argument(
                    "risk aversion too high for these parameters (sinh overflow"
                    " in the AC trajectory) -- reduce riskAversion or"
                    " shorten the horizon");
        }
    }
    auto n = static_cast<std::int64_t>(weights.size());
    if (durationMillis < 0 || durationMillis > std::numeric_limits<std::int64_t>::max() / n) {
        throw std::invalid_argument("durationMillis out of range for " + std::to_string(n) + " slices");
    }
    auto parent = static_cast<std::int64_t>(std::llround(params.totalShares));
    std::vector<std::int64_t> quantities = vwap_scheduler::allocateProportionally(parent, weights);
    std::vector<Slice> out;
    out.reserve(weights.size());
    for (std::int64_t i = 0; i < n; i++) {
        out.push_back(Slice{durationMillis * i / n, quantities[static_cast<std::size_t>(i)]});
    }
    return out;
}

double riskAversionForFrontLoad(const AlmgrenChriss::Params& base, double frontLoadFraction) {
    if (frontLoadFraction <= 1.0 / base.intervals || frontLoadFraction >= 1) {
        throw std::invalid_argument("frontLoadFraction must exceed the TWAP slice and be below 1");
    }
    double lo = 0;
    double hi = 1;
    // Grow hi until it front-loads enough. A NaN fraction (sinh overflow
    // at huge lambda) counts as "more than enough": it stops the growth
    // and bisection then converges back below the overflow boundary.
    for (int i = 0; i < 60 && frontLoadOf(base, hi) < frontLoadFraction; i++) {
        hi *= 4;
    }
    for (int i = 0; i < 80; i++) {
        double mid = (lo + hi) / 2;
        if (frontLoadOf(base, mid) < frontLoadFraction) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    double lambda = (lo + hi) / 2;
    // Never return silently-wrong urgency: verify the calibration landed.
    double achieved = frontLoadOf(base, lambda);
    if (!(std::abs(achieved - frontLoadFraction) < 0.01)) {
        char buf[256];
        std::snprintf(buf, sizeof(buf),
                     "front-load %.2f unreachable for these parameters"
                     " (best achievable near %.3f) -- the AC trajectory"
                     " overflows before front-loading that hard",
                     frontLoadFraction, achieved);
        throw std::invalid_argument(buf);
    }
    return lambda;
}

}  // namespace implementation_shortfall_scheduler
}  // namespace quantfinlib
