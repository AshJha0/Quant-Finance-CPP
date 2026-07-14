// Port of com.quantfinlib.microstructure.MarketImpactModel: temporary /
// permanent market impact for large trades, parameterized by ADV and
// daily volatility — the square-root law plus an Almgren-Chriss style
// linear decomposition. All results in basis points of the arrival
// price. Java's nullable estimate() becomes std::optional: an empty
// optional means the window has no volume (impact unknowable — callers
// charge their flat costs and skip impact).
#pragma once

#include <optional>

#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {

class MarketImpactModel {
public:
    MarketImpactModel(double adv, double dailyVolatility);

    /** Returns a copy with calibrated coefficients (square-root Y, temporary eta bps, permanent gamma). */
    MarketImpactModel withCoefficients(double y, double etaBps, double gamma) const;

    /**
     * Estimates a model from a series' trailing window: ADV as the mean
     * volume, daily vol as the stdev of close-to-close returns. Empty when
     * the window has no volume. Throws std::invalid_argument when
     * index < window (reading before bar 0 is a bug, not a degradation).
     */
    static std::optional<MarketImpactModel> estimate(const BarSeries& series, int index,
                                                     int window);

    /** Square-root-law total impact for an order of `quantity`. */
    double squareRootImpactBps(double quantity) const;

    /** Temporary (execution-rate) impact at the given participation rate in [0, 1]. */
    double temporaryImpactBps(double participationRate) const;

    /** Permanent (information) impact, linear in size relative to ADV. */
    double permanentImpactBps(double quantity) const;

    /**
     * Expected implementation cost of executing `quantity` at the given
     * participation rate: half the permanent impact plus the full
     * temporary impact.
     */
    double expectedCostBps(double quantity, double participationRate) const;

private:
    MarketImpactModel(double adv, double dailyVolatility, double y, double etaBps, double gamma);

    double adv_;
    double dailyVolatility_;
    double y_;        // square-root law coefficient (~1)
    double etaBps_;   // temporary impact bps at 100% participation
    double gamma_;    // permanent impact coefficient
};

}  // namespace quantfinlib
