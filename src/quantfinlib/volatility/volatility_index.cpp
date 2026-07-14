#include "quantfinlib/volatility/volatility_index.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {

double VolatilityIndex::index(const std::vector<double>& strikes,
                              const std::vector<double>& putMids,
                              const std::vector<double>& callMids,
                              double forward, double rate, double tYears) {
    const double inf = std::numeric_limits<double>::infinity();
    int n = static_cast<int>(strikes.size());
    if (n < 3 || static_cast<int>(putMids.size()) != n
            || static_cast<int>(callMids.size()) != n) {
        throw std::invalid_argument("need >= 3 aligned strikes/puts/calls");
    }
    if (!(tYears > 0) || tYears == inf) {
        throw std::invalid_argument("tYears must be positive and finite");
    }
    if (!std::isfinite(rate)) {
        throw std::invalid_argument("rate must be finite");
    }
    double prev = 0;
    for (int i = 0; i < n; i++) {
        if (!(strikes[i] > prev) || strikes[i] == inf) {
            throw std::invalid_argument("strikes must be ascending, positive and finite");
        }
        if (!(putMids[i] >= 0) || putMids[i] == inf
                || !(callMids[i] >= 0) || callMids[i] == inf) {
            throw std::invalid_argument("option mids must be >= 0 and finite");
        }
        prev = strikes[i];
    }
    if (!(forward > strikes[0] && forward < strikes[n - 1])) {
        throw std::invalid_argument("forward " + std::to_string(forward)
                + " must sit strictly inside the strike range — the index "
                  "cannot be measured from extrapolation");
    }

    // K0: highest strike at or below the forward.
    int pivot = 0;
    for (int i = 0; i < n; i++) {
        if (strikes[i] <= forward) {
            pivot = i;
        }
    }
    // e^{+rT}: FORWARD-values the option mids (the CBOE construction)
    // — this is a growth factor, not a discount factor; "fixing" it
    // to e^{-rT} would introduce a 2rT relative error.
    double growth = std::exp(rate * tYears);
    double sum = 0;
    for (int i = 0; i < n; i++) {
        double dk = i == 0 ? strikes[1] - strikes[0]
                : i == n - 1 ? strikes[n - 1] - strikes[n - 2]
                : (strikes[i + 1] - strikes[i - 1]) / 2;
        double q = i < pivot ? putMids[i]
                : i > pivot ? callMids[i]
                : (putMids[i] + callMids[i]) / 2;    // the K0 straddle average
        sum += dk / (strikes[i] * strikes[i]) * growth * q;
    }
    double k0 = strikes[pivot];
    double variance = (2 / tYears) * sum
            - (1 / tYears) * std::pow(forward / k0 - 1, 2);
    if (!(variance > 0)) {
        throw std::invalid_argument("chain implies non-positive variance ("
                + std::to_string(variance) + ") — the quotes are inconsistent");
    }
    return std::sqrt(variance);
}

}  // namespace quantfinlib
