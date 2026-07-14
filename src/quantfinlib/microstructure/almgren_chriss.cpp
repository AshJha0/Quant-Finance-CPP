#include "quantfinlib/microstructure/almgren_chriss.hpp"

#include <cmath>
#include <stdexcept>

namespace quantfinlib {

AlmgrenChriss::Params::Params(double totalShares_, double horizon_, int intervals_, double volatility_,
                              double temporaryImpact_, double permanentImpact_, double riskAversion_)
        : totalShares(totalShares_),
          horizon(horizon_),
          intervals(intervals_),
          volatility(volatility_),
          temporaryImpact(temporaryImpact_),
          permanentImpact(permanentImpact_),
          riskAversion(riskAversion_) {
    if (totalShares <= 0 || horizon <= 0 || intervals < 1) {
        throw std::invalid_argument("need positive size, horizon, intervals");
    }
    if (temporaryImpact <= 0 || riskAversion < 0 || volatility < 0) {
        throw std::invalid_argument("need eta > 0, lambda >= 0, sigma >= 0");
    }
}

AlmgrenChriss::Params AlmgrenChriss::Params::withRiskAversion(double lambda) const {
    return Params(totalShares, horizon, intervals, volatility, temporaryImpact, permanentImpact, lambda);
}

namespace {
double acoshImpl(double x) {
    return std::log(x + std::sqrt(x * x - 1));
}
}  // namespace

AlmgrenChriss::Trajectory AlmgrenChriss::optimalTrajectory(const Params& p) {
    int n = p.intervals;
    double tau = p.horizon / n;
    double etaTilde = p.temporaryImpact - 0.5 * p.permanentImpact * tau;
    if (etaTilde <= 0) {
        throw std::invalid_argument(
                "eta - gamma*tau/2 must be positive; shorten intervals or check impacts");
    }
    double kappa = 0;
    double lambdaSigma2 = p.riskAversion * p.volatility * p.volatility;
    if (lambdaSigma2 > 0) {
        double coshKappaTau = 1 + lambdaSigma2 * tau * tau / (2 * etaTilde);
        kappa = acoshImpl(coshKappaTau) / tau;
    }

    std::vector<double> holdings(static_cast<std::size_t>(n) + 1, 0.0);
    double x = p.totalShares;
    if (kappa * p.horizon < 1e-9) {
        // Risk-neutral limit: linear (TWAP) trajectory.
        for (int j = 0; j <= n; j++) {
            holdings[static_cast<std::size_t>(j)] = x * (1.0 - static_cast<double>(j) / n);
        }
    } else {
        double denom = std::sinh(kappa * p.horizon);
        for (int j = 0; j <= n; j++) {
            holdings[static_cast<std::size_t>(j)] = x * std::sinh(kappa * (p.horizon - j * tau)) / denom;
        }
    }
    holdings[static_cast<std::size_t>(n)] = 0;

    std::vector<double> trades(static_cast<std::size_t>(n), 0.0);
    double expectedCost = 0.5 * p.permanentImpact * x * x;
    double variance = 0;
    for (int j = 0; j < n; j++) {
        trades[static_cast<std::size_t>(j)] =
                holdings[static_cast<std::size_t>(j)] - holdings[static_cast<std::size_t>(j + 1)];
        expectedCost += etaTilde / tau * trades[static_cast<std::size_t>(j)] * trades[static_cast<std::size_t>(j)];
        variance += p.volatility * p.volatility * tau * holdings[static_cast<std::size_t>(j + 1)]
                * holdings[static_cast<std::size_t>(j + 1)];
    }
    return Trajectory{holdings, trades, kappa, expectedCost, variance};
}

AlmgrenChriss::Trajectory AlmgrenChriss::twap(const Params& p) {
    return optimalTrajectory(p.withRiskAversion(0));
}

std::vector<AlmgrenChriss::Trajectory> AlmgrenChriss::efficientFrontier(
        const Params& base, const std::vector<double>& riskAversions) {
    std::vector<Trajectory> out;
    out.reserve(riskAversions.size());
    for (double lambda : riskAversions) {
        out.push_back(optimalTrajectory(base.withRiskAversion(lambda)));
    }
    return out;
}

}  // namespace quantfinlib
