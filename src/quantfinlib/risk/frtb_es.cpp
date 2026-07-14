#include "quantfinlib/risk/frtb_es.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/risk/var_engine.hpp"

namespace quantfinlib {
namespace frtb_es {

double es975(const std::vector<double>& losses) {
    return var_engine::tail(losses, 0.975).expectedShortfall;
}

double liquidityHorizonEs(const std::vector<double>& esByHorizon,
                          const std::vector<int>& horizons) {
    if (esByHorizon.size() != horizons.size() || horizons.size() < 1
            || horizons[0] != 10) {
        throw std::invalid_argument("need aligned arrays with horizons starting at 10");
    }
    double sumSq = 0;
    for (std::size_t j = 0; j < horizons.size(); j++) {
        if (j > 0 && horizons[j] <= horizons[j - 1]) {
            throw std::invalid_argument("horizons must ascend");
        }
        if (!(esByHorizon[j] >= 0)
                || esByHorizon[j] == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("ES terms must be >= 0 and finite");
        }
        int prev = j == 0 ? 0 : horizons[j - 1];
        double scaled = esByHorizon[j] * std::sqrt((horizons[j] - prev) / 10.0);
        sumSq += scaled * scaled;
    }
    return std::sqrt(sumSq);
}

double stressCalibratedEs(double esCurrentFull, double esStressedReduced,
                          double esCurrentReduced) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    if (!(esCurrentFull >= 0) || esCurrentFull == inf
            || !(esStressedReduced >= 0) || esStressedReduced == inf
            || !(esCurrentReduced > 0) || esCurrentReduced == inf) {
        throw std::invalid_argument(
                "ES inputs must be non-negative and finite (current reduced > 0)");
    }
    double ratio = std::max(1.0, esStressedReduced / esCurrentReduced);
    return esCurrentFull * ratio;
}

TrafficLight trafficLightOf(int exceptions250d) {
    if (exceptions250d < 0) {
        throw std::invalid_argument("exceptions must be >= 0");
    }
    if (exceptions250d <= 4) {
        return TrafficLight::GREEN;
    }
    return exceptions250d <= 9 ? TrafficLight::AMBER : TrafficLight::RED;
}

}  // namespace frtb_es
}  // namespace quantfinlib
