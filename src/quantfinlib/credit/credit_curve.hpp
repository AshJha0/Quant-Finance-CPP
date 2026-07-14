// Port of com.quantfinlib.credit.CreditCurve (Java): piecewise-constant
// hazard rates bootstrapped from CDS par spreads — walk the quotes from
// shortest to longest, at each pillar solving for the one hazard rate
// that reprices that maturity's CDS to zero upfront given everything
// already solved.
//
// The HAZARD RATE h(t) is the instantaneous default intensity; SURVIVAL
// Q(t) = exp(-integral of h) is a product of exponentials under a
// piecewise-constant h, evaluated exactly. The CREDIT TRIANGLE
// spread ~ h * (1 - R) falls out on a flat curve (pinned by tests).
//
// Leg discretization (stated): quarterly grid, premium leg
// S * sum 0.25 * DF(t_i) * Q(t_i) plus the standard accrual-on-default
// half-period term, protection leg (1-R) * sum DF(t_i) * (Q(t_{i-1}) -
// Q(t_i)) — the textbook discrete form (O(dt) bias vs the integral,
// ~0.1bp at these grids, stated not hidden). Solving is bisection per
// pillar on h in [1e-9, 10] with an explicit bracket check — a quote no
// hazard can explain throws rather than returning the bound.
#pragma once

#include <vector>

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {

class CreditCurve {
public:
    /**
     * Bootstraps from CDS par spreads.
     * tenorYears: ascending integer-year pillars, >= 1;
     * parSpreads: par CDS spreads (decimal: 0.01 = 100bp), > 0;
     * recovery: assumed recovery rate in [0, 1);
     * discount: risk-free discounting curve.
     */
    static CreditCurve bootstrap(const std::vector<int>& tenorYears,
                                 const std::vector<double>& parSpreads,
                                 double recovery, const YieldCurve& discount);

    /** Survival probability Q(t), exact under piecewise-constant hazards. */
    double survivalProbability(double t) const;

    /** Cumulative default probability 1 - Q(t). */
    double defaultProbability(double t) const;

    /** The hazard rate in force at time t (flat beyond the last pillar). */
    double hazard(double t) const;

    double recovery() const { return recovery_; }

    /** The quarterly grid step shared with cds_pricer (Java: package-private). */
    static double gridStep() { return 0.25; }

private:
    CreditCurve(std::vector<double> pillarTimes, std::vector<double> hazards,
                double recovery);

    double upfrontWithPillar(std::size_t k, double trialHazard, double spread,
                             double maturity, const YieldCurve& discount);

    std::vector<double> pillarTimes_;  // ascending, years
    std::vector<double> hazards_;      // piecewise-constant on (prev, pillar]
    double recovery_;
};

}  // namespace quantfinlib
