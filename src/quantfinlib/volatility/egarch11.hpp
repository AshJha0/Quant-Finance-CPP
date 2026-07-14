// Port of com.quantfinlib.volatility.Egarch11 (Java): Nelson's exponential
// GARCH(1,1) — log-variance dynamics
//   ln h_t = omega + beta*ln h_{t-1} + alpha*(|z_{t-1}| - sqrt(2/pi)) + gamma*z_{t-1}
// with standardized shocks z = r/sqrt(h). Grid MLE over the empirically
// plausible box; omega targets the sample's log variance. One-step-ahead
// nextVariance is exact; multi-step forecasts are deliberately NOT offered
// (iterating the log recursion forecasts the MEDIAN variance, not the mean —
// use Garch11/GjrGarch11 for honest multi-step horizons).
#pragma once

#include <vector>

namespace quantfinlib {

class Egarch11 {
public:
    struct Params {
        double omega;
        double alpha;
        double gamma;
        double beta;
        double logLikelihood;

        /** Long-run (unconditional) LOG variance omega/(1-beta). */
        double unconditionalLogVariance() const { return omega / (1 - beta); }
    };

    Egarch11() = delete;

    /** Fits EGARCH(1,1) to (demeaned) returns by grid MLE. */
    static Params fit(const std::vector<double>& returns);

    /** Conditional variance series under the fitted parameters. */
    static std::vector<double> conditionalVariances(const std::vector<double>& returns,
                                                    const Params& p);

    /** One-step-ahead variance — EXACT (tomorrow's ln h is deterministic today). */
    static double nextVariance(const std::vector<double>& returns, const Params& p);
};

}  // namespace quantfinlib
