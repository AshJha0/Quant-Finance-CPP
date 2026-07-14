// Port of com.quantfinlib.pricing.ForwardCurve (Java): implied FX forward
// curve construction from market outright forwards, with interpolation
// (linear in forward points between pillars; flat-slope extrapolation
// beyond the last pillar), implied cc rate differentials, and
// covered-interest-parity arbitrage checks against SIMPLE deposit rates.
// The cc-vs-simple convention split is deliberate: each method matches
// how its own input is quoted.
#pragma once

#include <map>

namespace quantfinlib {

class ForwardCurve {
public:
    explicit ForwardCurve(double spot);

    ForwardCurve& addPoint(double tenorYears, double outrightForward);

    double spot() const;

    /** Interpolated outright forward at the tenor. */
    double forward(double tenorYears) const;

    /** Forward points at the tenor (outright minus spot). */
    double forwardPoints(double tenorYears) const;

    /**
     * Implied continuously-compounded rate differential (domestic minus
     * foreign) from covered interest parity: F = S * e^{(rd - rf) t}.
     */
    double impliedRateDifferential(double tenorYears) const;

    /** CIP-theoretical forward from SIMPLE deposit rates. */
    static double theoreticalForward(double spot, double domesticRate,
                                     double foreignRate, double tenorYears);

    /**
     * CIP arbitrage check: market forward versus the deposit-implied
     * forward, in basis points (positive = market forward rich).
     */
    double mispricingBps(double tenorYears, double domesticRate,
                         double foreignRate) const;

private:
    double spot_;
    std::map<double, double> outrights_;  // tenorYears -> outright forward
};

}  // namespace quantfinlib
