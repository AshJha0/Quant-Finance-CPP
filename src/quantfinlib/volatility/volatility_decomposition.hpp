// Port of com.quantfinlib.volatility.VolatilityDecomposition (Java):
// systematic vs idiosyncratic volatility via a single-factor (CAPM-style)
// regression:
//   Var(asset) = beta^2*Var(market) + Var(residual)
// with beta = Cov(asset, market)/Var(market). The split is exact (OLS
// residuals are uncorrelated with the regressor by construction); R^2 is the
// systematic share. Sample moments (n-1) throughout.
#pragma once

#include <vector>

namespace quantfinlib {

class VolatilityDecomposition {
public:
    struct Decomposition {
        double beta;                    // Cov(a,m)/Var(m)
        double totalVariance;           // per-period Var(asset)
        double systematicVariance;      // beta^2 * Var(market)
        double idiosyncraticVariance;   // the residual: total - systematic (>= 0)
        double rSquared;                // systematic share of total, in [0, 1]

        /** Annualized systematic volatility. */
        double systematicVol(int periodsPerYear) const;

        /** Annualized idiosyncratic volatility. */
        double idiosyncraticVol(int periodsPerYear) const;

        /** Annualized total volatility. */
        double totalVol(int periodsPerYear) const;
    };

    VolatilityDecomposition() = delete;

    /**
     * Decomposes an asset's variance against a market/benchmark series.
     * Both series must be aligned with >= 30 finite observations, and the
     * benchmark must carry variance.
     */
    static Decomposition decompose(const std::vector<double>& assetReturns,
                                   const std::vector<double>& marketReturns);
};

}  // namespace quantfinlib
