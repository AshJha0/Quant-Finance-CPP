// Port of com.quantfinlib.volatility.HarRv (Java): Corsi's Heterogeneous
// AutoRegressive realized-volatility model,
//   RV_{t+1} = c + bd*RV_t + bw*RVbar_t(5) + bm*RVbar_t(22) + eps.
// Plain OLS on the normal equations (no optimizer); the one-step forecast is
// floored at zero (a negative variance forecast is an extrapolation artifact,
// not a market view).
#pragma once

#include <vector>

namespace quantfinlib {

class HarRv {
public:
    /** Fitted coefficients: rv+ = c + bd*d + bw*w + bm*m. */
    struct Params {
        double intercept;
        double betaDaily;
        double betaWeekly;
        double betaMonthly;
    };

    HarRv() = delete;

    /**
     * Fits by OLS. Needs enough history for the monthly window plus a
     * meaningful regression sample: >= 60 finite non-negative observations.
     */
    static Params fit(const std::vector<double>& realizedVariance);

    /**
     * One-step-ahead RV forecast from the series' most recent day/week/month,
     * floored at zero.
     */
    static double forecast(const std::vector<double>& realizedVariance, const Params& p);
};

}  // namespace quantfinlib
