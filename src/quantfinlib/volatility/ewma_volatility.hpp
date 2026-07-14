// Port of com.quantfinlib.volatility.EwmaVolatility (Java): RiskMetrics-style
// exponentially weighted moving average variance,
//   h_t = lambda*h_{t-1} + (1-lambda)*r_{t-1}^2,
// seeded with the sample variance. IllegalArgumentException maps to
// std::invalid_argument.
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

    /** One-step-ahead volatility forecast (per period). */
    double latestVol(const std::vector<double>& returns) const;

    double annualizedVol(const std::vector<double>& returns, int periodsPerYear) const;

private:
    double lambda_;
};

}  // namespace quantfinlib
