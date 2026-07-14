// Port of com.quantfinlib.pricing.IncrementalGreeks (Java): tick-frequency
// Greek estimation without tick-frequency repricing. A full Black-Scholes
// evaluation anchors the position; every tick updates price/delta by the
// delta-gamma Taylor expansion:
//   price(S) ~ price0 + delta0*(S-S0) + 0.5*gamma0*(S-S0)^2
//   delta(S) ~ delta0 + gamma0*(S-S0)
// needsReprice() tells the slow path when spot has drifted far enough to
// re-anchor. Single-threaded by design; onTick never allocates.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {

class IncrementalGreeks {
public:
    /**
     * Full reprice: re-anchors the expansion. Call from the pricing/slow
     * thread — at start-up, on needsReprice, on vol or rate marks.
     */
    void reprice(OptionType type, double spot, double strike, double rate,
                 double carry, double vol, double timeYears);

    /** The hot path: delta-gamma update from the anchor. */
    void onTick(double spot);

    /** Whether spot has drifted beyond maxSpotDrift from the anchor. */
    bool needsReprice(double maxSpotDrift) const;

    /** Tick-fresh price estimate (per unit; scale by position externally). */
    double estimatedPrice() const;

    /** Tick-fresh delta estimate. */
    double estimatedDelta() const;

    /** Anchor gamma (constant between reprices). */
    double gamma() const;

    /** Anchor vega — vol risk only changes on reprice, not per tick. */
    double vega() const;

    /** Anchor theta. */
    double theta() const;

    /** The spot the expansion is anchored at (NaN before the first reprice). */
    double anchorSpot() const;

private:
    // Anchor state from the last full reprice.
    double baseSpot_ = nan_();
    double basePrice_ = 0;
    double baseDelta_ = 0;
    double baseGamma_ = 0;
    double baseVega_ = 0;
    double baseTheta_ = 0;

    // Tick-fresh estimates.
    double lastSpot_ = nan_();
    double estimatedPrice_ = 0;
    double estimatedDelta_ = 0;

    static double nan_();
};

}  // namespace quantfinlib
