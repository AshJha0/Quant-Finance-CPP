#include "quantfinlib/crb/crb_auto_hedger.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/crb/hedge_optimizer.hpp"

namespace quantfinlib {

namespace {
double signum(double x) {
    return x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0);
}
}  // namespace

CrbAutoHedger::CrbAutoHedger(std::vector<double> limits, double resetFraction,
                             std::int64_t cooldownIntervals)
    : limits_(std::move(limits)), resetFraction_(resetFraction),
      cooldownIntervals_(cooldownIntervals) {
    for (double l : limits_) {
        if (!(l > 0) || l == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("every limit must be positive and finite");
        }
    }
    if (!(resetFraction > 0 && resetFraction <= 1)) {
        throw std::invalid_argument("resetFraction must be in (0, 1]");
    }
    if (cooldownIntervals < 0) {
        throw std::invalid_argument("cooldownIntervals must be >= 0");
    }
}

void CrbAutoHedger::requireLength(const std::vector<double>& exposures) const {
    if (exposures.size() != limits_.size()) {
        throw std::invalid_argument("exposures length != limits length");
    }
    for (double e : exposures) {
        if (!std::isfinite(e)) {
            throw std::invalid_argument("exposures must be finite");
        }
    }
}

bool CrbAutoHedger::breached(const std::vector<double>& exposures) const {
    requireLength(exposures);
    for (std::size_t f = 0; f < limits_.size(); f++) {
        if (std::fabs(exposures[f]) > limits_[f]) {
            return true;
        }
    }
    return false;
}

bool CrbAutoHedger::stillBreached(const std::vector<double>& residual) const {
    for (std::size_t f = 0; f < limits_.size(); f++) {
        if (std::fabs(residual[f]) > limits_[f]) {
            return true;
        }
    }
    return false;
}

std::vector<CrbAutoHedger::HedgeOrder> CrbAutoHedger::check(
    const std::vector<double>& exposures, const crb_auto_hedger::Matrix& covariance,
    const crb_auto_hedger::Matrix& loadings, const std::vector<double>& costPerUnit,
    double costWeight, std::int64_t nowInterval) {
    requireLength(exposures);
    if (!breached(exposures)) {
        return {};
    }
    if (hasHedged_ && nowInterval - lastHedgeInterval_ < cooldownIntervals_) {
        return {};
    }
    std::vector<double> excess(limits_.size(), 0.0);
    for (std::size_t f = 0; f < limits_.size(); f++) {
        double band = limits_[f] * resetFraction_;
        if (std::fabs(exposures[f]) > limits_[f]) {
            excess[f] = signum(exposures[f]) * (std::fabs(exposures[f]) - band);
        }
    }
    std::vector<double> h = hedge_optimizer::hedge(excess, covariance, loadings, costPerUnit,
                                                   costWeight);
    if (stillBreached(hedge_optimizer::residual(exposures, loadings, h))) {
        h = hedge_optimizer::hedge(excess, covariance, loadings, costPerUnit, 0);
    }
    double maxH = 0;
    for (double v : h) {
        maxH = std::max(maxH, std::fabs(v));
    }
    double dust = 1e-6 * maxH;
    std::vector<HedgeOrder> orders;
    for (std::size_t i = 0; i < h.size(); i++) {
        if (std::fabs(h[i]) > dust) {
            orders.push_back({static_cast<int>(i), h[i]});
        }
    }
    if (!orders.empty()) {
        hasHedged_ = true;
        lastHedgeInterval_ = nowInterval;
        hedgesEmitted_++;
    }
    return orders;
}

}  // namespace quantfinlib
