// Port of com.quantfinlib.crb.CrbAutoHedger (Java): the central risk
// book's hedging loop — per-factor exposure BANDS, a cost-aware hedge when
// breached, and a cooldown so the book does not chase its own hedges.
// Inside the bands, warehouse; on a breach, hedge the breached factors
// back to resetFraction of the limit through HedgeOptimizer, cost-aware
// first, escalating to zero cost weight if any factor stays outside its
// hard limit. Time is a caller-supplied interval counter (no wall clock).
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {
namespace crb_auto_hedger {
using Matrix = std::vector<std::vector<double>>;
}

class CrbAutoHedger {
public:
    /** One instrument's hedge instruction. */
    struct HedgeOrder {
        int instrument;
        double notional;
    };

    /**
     * @param limits            per-factor |exposure| limits (registry
     *                          order; > 0 each)
     * @param resetFraction     post-hedge target as a fraction of the
     *                          limit, in (0, 1]
     * @param cooldownIntervals min intervals between hedges, >= 0
     */
    CrbAutoHedger(std::vector<double> limits, double resetFraction,
                 std::int64_t cooldownIntervals);

    /** True when any factor sits outside its band. */
    bool breached(const std::vector<double>& exposures) const;

    /**
     * The hedging decision for this interval. Empty when inside all bands
     * or still cooling down; only the excess beyond resetFraction*limit on
     * the breached factors is hedged.
     */
    std::vector<HedgeOrder> check(const std::vector<double>& exposures,
                                  const crb_auto_hedger::Matrix& covariance,
                                  const crb_auto_hedger::Matrix& loadings,
                                  const std::vector<double>& costPerUnit, double costWeight,
                                  std::int64_t nowInterval);

    /** The band the book hedges back INTO for a factor. */
    double targetBand(int factor) const { return limits_[factor] * resetFraction_; }

    std::int64_t hedgesEmitted() const { return hedgesEmitted_; }

private:
    bool stillBreached(const std::vector<double>& residual) const;
    void requireLength(const std::vector<double>& exposures) const;

    std::vector<double> limits_;
    double resetFraction_;
    std::int64_t cooldownIntervals_;
    bool hasHedged_ = false;
    std::int64_t lastHedgeInterval_ = 0;
    std::int64_t hedgesEmitted_ = 0;
};

}  // namespace quantfinlib
