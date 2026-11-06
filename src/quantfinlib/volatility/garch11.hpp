// Port of com.quantfinlib.volatility.Garch11 (Java): GARCH(1,1) with
// Gaussian maximum-likelihood fitting,
//   h_t = omega + alpha*r_{t-1}^2 + beta*h_{t-1}.
// Estimation is full maximum likelihood over (omega, alpha, beta), and nu for
// Student-t innovations, with h_0 at the sample variance: Nelder-Mead from
// three starts in an unconstrained reparameterisation that keeps omega > 0,
// alpha, beta >= 0 and alpha + beta < 1. Standard errors are the inverse
// observed information and the Bollerslev-Wooldridge sandwich.
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

    enum class Innovation { GAUSSIAN, STUDENT_T };

    struct Fit {
        Params params;
        Innovation innovation;
        double nu;                         // NaN for Gaussian
        std::vector<double> standardErrors;       // omega, alpha, beta[, nu]; NaN if not PD
        std::vector<double> robustStandardErrors;
    };

    Garch11() = delete;

    /** Gaussian MLE of GARCH(1,1) on demeaned returns. Needs 100 finite returns. */
    static Params fit(const std::vector<double>& returns);

    /** MLE with the chosen innovation law, with standard errors. */
    static Fit estimate(const std::vector<double>& returns, Innovation innovation);

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
