// Port of com.quantfinlib.commodities.CommodityCurve (Java): commodity
// futures curve. CONTANGO (upward: deferred contracts above spot)
// charges a long position negative ROLL YIELD every roll; BACKWARDATION
// (downward) pays the long for rolling.
//   annualized roll yield between two tenors:
//     ln(F(near)/F(far)) / (far - near) — positive in backwardation;
//   implied carry versus spot: from F = S * exp((r + u - y) * t), the
//     market-implied u - y (storage cost minus convenience yield) is
//     ln(F(t)/S)/t - r;
//   shape tests: isContango()/isBackwardation() across the whole curve,
//     strict at every adjacent pillar pair (spot included as the base).
// Linear interpolation between pillar prices, NO extrapolation (asking
// for a price beyond the pillars throws — a commodity curve's wings are
// opinions, not data). Java's TreeMap maps to std::map.
#pragma once

#include <map>
#include <vector>

namespace quantfinlib {

class CommodityCurve {
public:
    /**
     * spot: spot price, > 0; tenorYears: ascending futures tenors, all
     * > 0; prices: futures prices per tenor, all > 0.
     */
    static CommodityCurve of(double spot, const std::vector<double>& tenorYears,
                             const std::vector<double>& prices);

    /** Interpolated futures price; throws beyond the pillars (no extrapolation). */
    double price(double tenorYears) const;

    /**
     * Annualized roll yield earned by a LONG rolling from `nearYears` to
     * `farYears`: positive in backwardation.
     */
    double annualizedRollYield(double nearYears, double farYears) const;

    /**
     * Market-implied storage-minus-convenience u - y (cc) at the tenor,
     * from F = S * exp((r + u - y) t). `rate` is the cc risk-free rate
     * to the tenor.
     */
    double impliedCarry(double tenorYears, double rate) const;

    /** Strictly upward at every adjacent pillar pair (deferred above near). */
    bool isContango() const;

    /** Strictly downward at every adjacent pillar pair. */
    bool isBackwardation() const;

    double spot() const { return spot_; }

private:
    explicit CommodityCurve(double spot) : spot_(spot) {}

    std::map<double, double> futures_;  // tenorYears -> price
    double spot_;
};

}  // namespace quantfinlib
