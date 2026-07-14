// Port of com.quantfinlib.volatility.Garch11 (Java): GARCH(1,1) with
// Gaussian maximum-likelihood fitting,
//   h_t = omega + alpha*r_{t-1}^2 + beta*h_{t-1}.
// Estimation uses variance targeting (omega = sampleVar*(1-alpha-beta)) and a
// deterministic coarse-to-fine grid search over (alpha, beta), spanning the
// FULL admissible box exactly as the Java source does.
#pragma once

#include <vector>

namespace quantfinlib {

class Garch11 {
public:
    struct Params {
        double omega;
        double alpha;
        double beta;
        double logLikelihood;

        double persistence() const { return alpha + beta; }

        double unconditionalVariance() const { return omega / (1 - alpha - beta); }
    };

    Garch11() = delete;

    /** Fits GARCH(1,1) to (demeaned) returns by MLE with variance targeting. */
    static Params fit(const std::vector<double>& returns);

    /** Conditional variance series under the fitted parameters (seeded at sample variance). */
    static std::vector<double> conditionalVariances(const std::vector<double>& returns,
                                                    const Params& params);

    /**
     * k-step-ahead variance forecast:
     * h_{T+k} = uncond + persistence^{k-1} (h_{T+1} - uncond) — mean-reverts
     * to the unconditional variance at the persistence rate.
     */
    static double forecastVariance(const std::vector<double>& returns,
                                   const Params& params, int horizon);
};

}  // namespace quantfinlib
