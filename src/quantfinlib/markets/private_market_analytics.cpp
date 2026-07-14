#include "quantfinlib/markets/private_market_analytics.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace private_market_analytics {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();

double npv(const std::vector<double>& cashflows, double rate) {
    double v = 0;
    for (std::size_t t = 0; t < cashflows.size(); t++) {
        v += cashflows[t] / std::pow(1 + rate, static_cast<double>(t));
    }
    return v;
}

void validateMultiples(double contributions, double distributions, double nav) {
    if (!(contributions > 0) || contributions == kInf) {
        throw std::invalid_argument("contributions must be positive and finite");
    }
    if (!(distributions >= 0) || distributions == kInf
            || !(nav >= 0) || nav == kInf) {
        throw std::invalid_argument("distributions and nav must be >= 0 and finite");
    }
}

double requireFinite(double v) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument("non-finite return");
    }
    return v;
}

}  // namespace

double irr(const std::vector<double>& cashflows) {
    if (cashflows.size() < 2) {
        throw std::invalid_argument("need >= 2 cash flows, got "
                                    + std::to_string(cashflows.size()));
    }
    bool hasNeg = false, hasPos = false;
    for (double cf : cashflows) {
        if (!std::isfinite(cf)) {
            throw std::invalid_argument("non-finite cash flow");
        }
        hasNeg |= cf < 0;
        hasPos |= cf > 0;
    }
    if (!hasNeg || !hasPos) {
        throw std::invalid_argument("cash flows never change sign: no IRR exists");
    }
    double lo = -0.9999, hi = 100;
    double npvLo = npv(cashflows, lo);
    double npvHi = npv(cashflows, hi);
    if (npvLo * npvHi > 0) {
        throw std::invalid_argument("no IRR in (-99.99%, 10000%)");
    }
    for (int i = 0; i < 300; i++) {
        double mid = 0.5 * (lo + hi);
        double v = npv(cashflows, mid);
        if (v * npvLo > 0) {
            lo = mid;
            npvLo = v;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

double tvpi(double contributions, double distributions, double nav) {
    validateMultiples(contributions, distributions, nav);
    return (distributions + nav) / contributions;
}

double dpi(double contributions, double distributions, double nav) {
    validateMultiples(contributions, distributions, nav);
    return distributions / contributions;
}

double rvpi(double contributions, double distributions, double nav) {
    validateMultiples(contributions, distributions, nav);
    return nav / contributions;
}

double ksPme(const std::vector<double>& contributions,
             const std::vector<double>& distributions,
             double terminalNav, const std::vector<double>& indexLevels) {
    std::size_t n = indexLevels.size();
    if (n < 2 || contributions.size() != n || distributions.size() != n) {
        throw std::invalid_argument("need aligned series of length >= 2");
    }
    double last = indexLevels[n - 1];
    double fvContrib = 0, fvDistrib = 0;
    for (std::size_t t = 0; t < n; t++) {
        if (!(indexLevels[t] > 0) || indexLevels[t] == kInf) {
            throw std::invalid_argument("index level must be positive: "
                                        + std::to_string(indexLevels[t]));
        }
        if (!(contributions[t] >= 0) || !(distributions[t] >= 0)
                || contributions[t] == kInf || distributions[t] == kInf) {
            throw std::invalid_argument(
                    "flows must be >= 0 and finite (amounts, not signed)");
        }
        double growth = last / indexLevels[t];
        fvContrib += contributions[t] * growth;
        fvDistrib += distributions[t] * growth;
    }
    if (!(terminalNav >= 0) || terminalNav == kInf) {
        throw std::invalid_argument("terminalNav must be >= 0 and finite");
    }
    if (fvContrib == 0) {
        throw std::invalid_argument("no contributions: PME undefined");
    }
    return (fvDistrib + terminalNav) / fvContrib;
}

std::vector<double> geltnerDesmooth(const std::vector<double>& observedReturns,
                                    double phi) {
    if (observedReturns.size() < 2) {
        throw std::invalid_argument("need >= 2 returns");
    }
    if (!(phi >= 0) || !(phi < 1)) {
        throw std::invalid_argument("phi must be in [0, 1), got " + std::to_string(phi));
    }
    std::vector<double> r(observedReturns.size());
    r[0] = requireFinite(observedReturns[0]);
    for (std::size_t t = 1; t < r.size(); t++) {
        double now = requireFinite(observedReturns[t]);
        r[t] = (now - phi * observedReturns[t - 1]) / (1 - phi);
    }
    return r;
}

}  // namespace private_market_analytics
}  // namespace quantfinlib
