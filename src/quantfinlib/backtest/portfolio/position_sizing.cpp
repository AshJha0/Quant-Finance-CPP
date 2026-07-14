#include "quantfinlib/backtest/portfolio/position_sizing.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {
namespace position_sizing {

double kellyFraction(double meanReturn, double variance) {
    return variance == 0 ? 0 : meanReturn / variance;
}

double halfKelly(double meanReturn, double variance) {
    return kellyFraction(meanReturn, variance) / 2;
}

double fixedFractionalQuantity(double equity, double riskFraction,
                               double entryPrice, double stopPrice) {
    double perShareRisk = std::fabs(entryPrice - stopPrice);
    if (perShareRisk == 0) {
        throw std::invalid_argument("entry equals stop: undefined risk");
    }
    return equity * riskFraction / perShareRisk;
}

std::vector<double> inverseVolatilityWeights(const std::vector<double>& vols) {
    std::vector<double> inv(vols.size());
    double sum = 0;
    for (std::size_t i = 0; i < vols.size(); i++) {
        if (vols[i] <= 0) {
            // Degenerate input: fall back to equal weight.
            std::fill(inv.begin(), inv.end(), 1.0 / static_cast<double>(vols.size()));
            return inv;
        }
        inv[i] = 1 / vols[i];
        sum += inv[i];
    }
    for (std::size_t i = 0; i < inv.size(); i++) {
        inv[i] /= sum;
    }
    return inv;
}

double volatilityTargetLeverage(double currentAnnualVol, double targetAnnualVol) {
    if (currentAnnualVol <= 0) {
        return 0;
    }
    return targetAnnualVol / currentAnnualVol;
}

}  // namespace position_sizing
}  // namespace quantfinlib
