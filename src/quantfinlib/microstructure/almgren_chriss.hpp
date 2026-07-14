// Port of com.quantfinlib.microstructure.AlmgrenChriss (2000): the trading
// trajectory that minimizes E[cost] + lambda*Var[cost] when liquidating X
// shares over a horizon, trading off temporary impact (fast execution is
// expensive) against price risk (slow execution is risky). Closed-form
// discrete solution: holdings decay as x_j = X * sinh(kappa*(T-t_j)) /
// sinh(kappa*T), with the urgency parameter kappa growing with risk
// aversion. lambda -> 0 recovers TWAP.
//
// Uses the same impact parameterization as MarketImpactModel: temporary
// impact eta (price concession per unit trade rate) and permanent impact
// gamma (per share). All quantities share one time unit (e.g. days): sigma
// is price volatility per sqrt(time), T the horizon in those units.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class AlmgrenChriss {
public:
    /**
     * @param totalShares     X, the parent order size
     * @param horizon         T, execution horizon (in the chosen time unit)
     * @param intervals       N trading intervals
     * @param volatility      sigma, price volatility per sqrt(time-unit) (price units)
     * @param temporaryImpact eta: price concession per unit of trade rate
     * @param permanentImpact gamma: permanent price move per share traded
     * @param riskAversion    lambda >= 0: variance penalty (0 = risk-neutral / TWAP)
     */
    struct Params {
        double totalShares;
        double horizon;
        int intervals;
        double volatility;
        double temporaryImpact;
        double permanentImpact;
        double riskAversion;

        Params(double totalShares_, double horizon_, int intervals_, double volatility_,
               double temporaryImpact_, double permanentImpact_, double riskAversion_);

        Params withRiskAversion(double lambda) const;
    };

    /**
     * The optimal schedule: holdings[j] is the position after interval j
     * (holdings[0] = X, holdings[N] = 0); trades[j] is sold in interval
     * j+1. Costs are in price*shares units versus the arrival price.
     */
    struct Trajectory {
        std::vector<double> holdings;
        std::vector<double> trades;
        double kappa;
        double expectedCost;
        double costVariance;
    };

    AlmgrenChriss() = delete;

    /** @throws std::invalid_argument if eta - gamma*tau/2 <= 0 (permanent impact too large for the interval size). */
    static Trajectory optimalTrajectory(const Params& p);

    /** The risk-neutral (lambda = 0) linear schedule, for comparison. */
    static Trajectory twap(const Params& p);

    /** Cost/risk frontier across risk aversions (for choosing the urgency). */
    static std::vector<Trajectory> efficientFrontier(const Params& base,
                                                      const std::vector<double>& riskAversions);
};

}  // namespace quantfinlib
