#include "quantfinlib/microstructure/market_impact_model.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {

MarketImpactModel::MarketImpactModel(double adv, double dailyVolatility)
        : MarketImpactModel(adv, dailyVolatility, 1.0, 20.0, 0.5) {}

MarketImpactModel::MarketImpactModel(double adv, double dailyVolatility, double y, double etaBps,
                                     double gamma)
        : adv_(adv), dailyVolatility_(dailyVolatility), y_(y), etaBps_(etaBps), gamma_(gamma) {
    if (adv <= 0) {
        throw std::invalid_argument("adv must be positive");
    }
}

MarketImpactModel MarketImpactModel::withCoefficients(double y, double etaBps,
                                                      double gamma) const {
    return MarketImpactModel(adv_, dailyVolatility_, y, etaBps, gamma);
}

std::optional<MarketImpactModel> MarketImpactModel::estimate(const BarSeries& series, int index,
                                                             int window) {
    if (index < window) {
        throw std::invalid_argument("index " + std::to_string(index) + " < estimation window "
                                    + std::to_string(window));
    }
    double advSum = 0;
    double mean = 0;
    for (int j = 0; j < window; j++) {
        advSum += series.volume(index - window + j + 1);
        mean += series.close(index - window + j + 1) / series.close(index - window + j) - 1;
    }
    double adv = advSum / window;
    if (adv <= 0) {
        return std::nullopt;
    }
    mean /= window;
    double var = 0;
    for (int j = 0; j < window; j++) {
        double r = series.close(index - window + j + 1) / series.close(index - window + j) - 1;
        var += (r - mean) * (r - mean);
    }
    return MarketImpactModel(adv, std::sqrt(var / window));
}

double MarketImpactModel::squareRootImpactBps(double quantity) const {
    return y_ * dailyVolatility_ * std::sqrt(quantity / adv_) * 1e4;
}

double MarketImpactModel::temporaryImpactBps(double participationRate) const {
    return etaBps_ * participationRate;
}

double MarketImpactModel::permanentImpactBps(double quantity) const {
    return gamma_ * dailyVolatility_ * (quantity / adv_) * 1e4;
}

double MarketImpactModel::expectedCostBps(double quantity, double participationRate) const {
    return permanentImpactBps(quantity) / 2 + temporaryImpactBps(participationRate);
}

}  // namespace quantfinlib
