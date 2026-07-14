#include "quantfinlib/credit/credit_curve.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "quantfinlib/credit/cds_pricer.hpp"

namespace quantfinlib {

CreditCurve::CreditCurve(std::vector<double> pillarTimes, std::vector<double> hazards,
                         double recovery)
    : pillarTimes_(std::move(pillarTimes)),
      hazards_(std::move(hazards)),
      recovery_(recovery) {}

CreditCurve CreditCurve::bootstrap(const std::vector<int>& tenorYears,
                                   const std::vector<double>& parSpreads,
                                   double recovery, const YieldCurve& discount) {
    std::size_t n = tenorYears.size();
    if (n == 0 || parSpreads.size() != n) {
        throw std::invalid_argument("need aligned, non-empty tenors/spreads");
    }
    if (!(recovery >= 0) || !(recovery < 1)) {
        throw std::invalid_argument("recovery must be in [0, 1), got "
                                    + std::to_string(recovery));
    }
    int prev = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (tenorYears[i] <= prev) {
            throw std::invalid_argument("tenors must be ascending positive integers");
        }
        prev = tenorYears[i];
        if (!(parSpreads[i] > 0)
                || parSpreads[i] == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("spread must be positive and finite: "
                                        + std::to_string(parSpreads[i]));
        }
    }
    std::vector<double> times(n);
    std::vector<double> hazards(n, 0.0);
    for (std::size_t i = 0; i < n; i++) {
        times[i] = tenorYears[i];
    }
    CreditCurve curve(times, hazards, recovery);
    for (std::size_t k = 0; k < n; k++) {
        double spread = parSpreads[k];
        double maturity = times[k];
        double lo = 1e-9, hi = 10;
        double upLo = curve.upfrontWithPillar(k, lo, spread, maturity, discount);
        double upHi = curve.upfrontWithPillar(k, hi, spread, maturity, discount);
        if (upLo * upHi > 0) {
            throw std::invalid_argument("pillar " + std::to_string(tenorYears[k])
                    + "y spread " + std::to_string(spread)
                    + " has no hazard in [1e-9, 10]");
        }
        for (int it = 0; it < 200; it++) {
            double mid = 0.5 * (lo + hi);
            double up = curve.upfrontWithPillar(k, mid, spread, maturity, discount);
            if (up * upLo > 0) {
                lo = mid;
                upLo = up;
            } else {
                hi = mid;
            }
        }
        curve.hazards_[k] = 0.5 * (lo + hi);
    }
    return curve;
}

double CreditCurve::upfrontWithPillar(std::size_t k, double trialHazard, double spread,
                                      double maturity, const YieldCurve& discount) {
    double saved = hazards_[k];
    hazards_[k] = trialHazard;
    double up = cds_pricer::protectionLegPv(*this, discount, maturity)
            - spread * cds_pricer::riskyAnnuity(*this, discount, maturity);
    hazards_[k] = saved;
    return up;
}

double CreditCurve::survivalProbability(double t) const {
    if (!(t >= 0) || t == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("t must be >= 0 and finite, got "
                                    + std::to_string(t));
    }
    double integral = 0;
    double from = 0;
    for (std::size_t i = 0; i < pillarTimes_.size() && from < t; i++) {
        double to = std::min(pillarTimes_[i], t);
        if (to > from) {
            integral += hazards_[i] * (to - from);
            from = to;
        }
    }
    if (from < t) {  // flat extrapolation beyond the last pillar
        integral += hazards_.back() * (t - from);
    }
    return std::exp(-integral);
}

double CreditCurve::defaultProbability(double t) const {
    return 1 - survivalProbability(t);
}

double CreditCurve::hazard(double t) const {
    if (!(t >= 0)) {
        throw std::invalid_argument("t must be >= 0, got " + std::to_string(t));
    }
    for (std::size_t i = 0; i < pillarTimes_.size(); i++) {
        if (t <= pillarTimes_[i]) {
            return hazards_[i];
        }
    }
    return hazards_.back();
}

}  // namespace quantfinlib
