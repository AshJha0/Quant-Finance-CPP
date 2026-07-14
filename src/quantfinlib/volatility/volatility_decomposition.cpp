#include "quantfinlib/volatility/volatility_decomposition.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

double VolatilityDecomposition::Decomposition::systematicVol(int periodsPerYear) const {
    return std::sqrt(systematicVariance * periodsPerYear);
}

double VolatilityDecomposition::Decomposition::idiosyncraticVol(int periodsPerYear) const {
    return std::sqrt(idiosyncraticVariance * periodsPerYear);
}

double VolatilityDecomposition::Decomposition::totalVol(int periodsPerYear) const {
    return std::sqrt(totalVariance * periodsPerYear);
}

VolatilityDecomposition::Decomposition VolatilityDecomposition::decompose(
        const std::vector<double>& assetReturns,
        const std::vector<double>& marketReturns) {
    if (assetReturns.size() != marketReturns.size() || assetReturns.size() < 30) {
        throw std::invalid_argument("need >= 30 aligned observations, got "
                                    + std::to_string(assetReturns.size()));
    }
    for (std::size_t i = 0; i < assetReturns.size(); i++) {
        if (!std::isfinite(assetReturns[i]) || !std::isfinite(marketReturns[i])) {
            throw std::invalid_argument("returns must be finite");
        }
    }
    double marketVar = math_utils::variance(marketReturns);
    if (!(marketVar > 0)) {
        throw std::invalid_argument(
                "the benchmark carries no variance — nothing to decompose against");
    }
    double cov = math_utils::covariance(assetReturns, marketReturns);
    double beta = cov / marketVar;
    double totalVar = math_utils::variance(assetReturns);
    double systematic = beta * beta * marketVar;      // = cov^2/Var(m)
    // Exact by the OLS identity; the max() only absorbs float dust
    // (Cauchy-Schwarz guarantees systematic <= total in exact math).
    double idiosyncratic = std::max(0.0, totalVar - systematic);
    double rSquared = totalVar > 0 ? std::min(1.0, systematic / totalVar) : 0;
    return Decomposition{beta, totalVar, systematic, idiosyncratic, rSquared};
}

}  // namespace quantfinlib
