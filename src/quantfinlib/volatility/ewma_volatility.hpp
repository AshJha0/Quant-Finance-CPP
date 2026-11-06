// Port of com.quantfinlib.volatility.EwmaVolatility (Java): RiskMetrics-style
// exponentially weighted moving average variance,
//   h_t = lambda*h_{t-1} + (1-lambda)*r_{t-1}^2,
// on raw returns. Seeding is causal: variances(returns) has no prior, so for
// t >= 1 it uses the bias-corrected weights
//   h_t = sum_{k<t} (1-lambda) lambda^{t-1-k} r_k^2 / (1 - lambda^t),
// which see only returns before t; h_0 has no information and is set to
// h_1 = r_0^2, the one entry that sees its own period's return. The
// two-argument form takes a prior variance from a training window and runs
// the plain recursion. IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <vector>

namespace quantfinlib {

class EwmaVolatility {
public:
    /** lambda must lie strictly inside (0, 1). */
    explicit EwmaVolatility(double lambda);

    /** The classic RiskMetrics daily decay (lambda = 0.94). */
    static EwmaVolatility riskMetrics();

    /**
     * Conditional variance series aligned with returns: variances[i] is the
     * estimate for period i, formed from information up to i-1.
     */
    std::vector<double> variances(const std::vector<double>& returns) const;

    /** Conditional variances from an explicit prior: h_0 = seedVariance > 0. */
    std::vector<double> variances(const std::vector<double>& returns,
                                  double seedVariance) const;

    /** One-step-ahead volatility forecast, same bias-corrected causal weights. */
    double latestVol(const std::vector<double>& returns) const;

    double annualizedVol(const std::vector<double>& returns, int periodsPerYear) const;

private:
    double lambda_;
};

}  // namespace quantfinlib
