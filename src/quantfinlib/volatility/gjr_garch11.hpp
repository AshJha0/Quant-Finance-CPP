// Port of com.quantfinlib.volatility.GjrGarch11 (Java): GJR-GARCH(1,1,1) —
// GARCH with the leverage term,
//   h_t = omega + (alpha + gamma*1{r<0}) r_{t-1}^2 + beta*h_{t-1}.
// Gaussian MLE with variance targeting (omega = sampleVar*(1-alpha-gamma/2-beta))
// and a deterministic coarse-to-fine grid over (alpha, gamma, beta) spanning
// the FULL admissible box; persistence is alpha + gamma/2 + beta.
#pragma once

#include <vector>

namespace quantfinlib {

class GjrGarch11 {
public:
    struct Params {
        double omega;
        double alpha;
        double gamma;
        double beta;
        double logLikelihood;

        /** alpha + gamma/2 + beta — the mean-reversion persistence under symmetric returns. */
        double persistence() const { return alpha + gamma / 2 + beta; }

        double unconditionalVariance() const { return omega / (1 - persistence()); }
    };

    GjrGarch11() = delete;

    /** Fits GJR-GARCH(1,1) to (demeaned) returns by MLE with variance targeting. */
    static Params fit(const std::vector<double>& returns);

    /** Conditional variance series under the fitted parameters (seeded at sample variance). */
    static std::vector<double> conditionalVariances(const std::vector<double>& returns,
                                                    const Params& params);

    /**
     * k-step-ahead variance forecast — mean-reverts to the unconditional
     * variance at the persistence rate, exactly as Garch11 but with the
     * asymmetric first step.
     */
    static double forecastVariance(const std::vector<double>& returns,
                                   const Params& params, int horizon);
};

}  // namespace quantfinlib
