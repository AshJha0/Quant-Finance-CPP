// Port of com.quantfinlib.volatility.VolatilityIndex (Java): a VIX-style
// market volatility index — the variance-swap replication (Carr-Madan /
// CBOE methodology, single expiry):
//   sigma^2 = (2/T) * sum_i (dK_i/K_i^2) * e^{+rT} * Q(K_i)
//             - (1/T) * (F/K0 - 1)^2
// where Q(K) is the out-of-the-money option mid (puts below the forward,
// calls above, the put/call average at the pivot K0), and e^{+rT} is a
// GROWTH factor (forward-values the mids), not a discount factor.
#pragma once

#include <vector>

namespace quantfinlib {

class VolatilityIndex {
public:
    VolatilityIndex() = delete;

    /**
     * The index (annualized volatility, e.g. 0.20 = "a VIX of 20") from one
     * expiry's chain.
     *
     * strikes  — ascending, >= 3, all > 0
     * putMids  — put mid prices per strike, >= 0, finite
     * callMids — call mid prices per strike, >= 0, finite
     * forward  — the forward F, strictly inside (strikes.front(), strikes.back())
     * rate     — continuously-compounded rate to expiry
     * tYears   — time to expiry, > 0
     */
    static double index(const std::vector<double>& strikes,
                        const std::vector<double>& putMids,
                        const std::vector<double>& callMids,
                        double forward, double rate, double tYears);
};

}  // namespace quantfinlib
