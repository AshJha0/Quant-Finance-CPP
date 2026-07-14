#include "quantfinlib/fx/swap_points_curve.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>

namespace quantfinlib {

SwapPointsCurve::Builder::Builder(CurrencyPair pair, LocalDate tradeDate, double spotRate)
    : pair_(std::move(pair)), tradeDate_(tradeDate), spotRate_(spotRate) {
    if (spotRate <= 0) {
        throw std::invalid_argument("spotRate must be > 0: " + std::to_string(spotRate));
    }
}

SwapPointsCurve::Builder& SwapPointsCurve::Builder::add(const std::string& tenor, double pips) {
    LocalDate date = pair_.tenorDate(tradeDate_, tenor);
    if (!date.isAfter(pair_.spotDate(tradeDate_))) {
        throw std::invalid_argument(
            "tenor " + tenor + " does not settle after spot; pre-spot legs (ON/TN) "
            "belong to the roll, not the forward curve");
    }
    tenors_.push_back(tenor);
    dates_.push_back(date);
    points_.push_back(pips * pair_.pipSize());
    return *this;
}

SwapPointsCurve SwapPointsCurve::Builder::build() const {
    if (dates_.empty()) {
        throw std::runtime_error("at least one pillar required");
    }
    LocalDate spot = pair_.spotDate(tradeDate_);
    std::size_t n = dates_.size();
    // Sort pillars by date (quotes may arrive in any order).
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [this](std::size_t a, std::size_t b) { return dates_[a] < dates_[b]; });
    std::vector<std::int64_t> days(n);
    std::vector<double> pts(n);
    std::vector<std::string> tns(n);
    for (std::size_t i = 0; i < n; i++) {
        std::size_t src = order[i];
        days[i] = LocalDate::daysBetween(spot, dates_[src]);
        pts[i] = points_[src];
        tns[i] = tenors_[src];
        if (i > 0 && days[i] == days[i - 1]) {
            throw std::invalid_argument("duplicate pillar date at " + tns[i]);
        }
    }
    return SwapPointsCurve(pair_, spot, spotRate_, std::move(days), std::move(pts),
                           std::move(tns));
}

SwapPointsCurve::SwapPointsCurve(CurrencyPair pair, LocalDate spotDate, double spotRate,
                                 std::vector<std::int64_t> pillarDays,
                                 std::vector<double> pillarPoints,
                                 std::vector<std::string> tenors)
    : pair_(std::move(pair)),
      spotDate_(spotDate),
      spotRate_(spotRate),
      pillarDays_(std::move(pillarDays)),
      pillarPoints_(std::move(pillarPoints)),
      tenors_(std::move(tenors)) {}

double SwapPointsCurve::forwardPoints(const LocalDate& valueDate) const {
    std::int64_t d = LocalDate::daysBetween(spotDate_, valueDate);
    if (d < 0) {
        throw std::invalid_argument("valueDate " + valueDate.toString() + " is before spot "
                                    + spotDate_.toString());
    }
    if (d == 0) {
        return 0;
    }
    std::size_t n = pillarDays_.size();
    // Before the first pillar: interpolate from (0 days, 0 points).
    if (d <= pillarDays_[0]) {
        return pillarPoints_[0] * (static_cast<double>(d) / static_cast<double>(pillarDays_[0]));
    }
    // Beyond the last pillar: extend the final segment's slope.
    if (d >= pillarDays_[n - 1]) {
        if (n == 1) {
            return pillarPoints_[0]
                   * (static_cast<double>(d) / static_cast<double>(pillarDays_[0]));
        }
        double slope = (pillarPoints_[n - 1] - pillarPoints_[n - 2])
                       / static_cast<double>(pillarDays_[n - 1] - pillarDays_[n - 2]);
        return pillarPoints_[n - 1] + slope * static_cast<double>(d - pillarDays_[n - 1]);
    }
    // Binary search for the bracketing pillars.
    std::size_t lo = 0;
    std::size_t hi = n - 1;
    while (hi - lo > 1) {
        std::size_t mid = (lo + hi) / 2;
        if (pillarDays_[mid] <= d) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    double w = static_cast<double>(d - pillarDays_[lo])
               / static_cast<double>(pillarDays_[hi] - pillarDays_[lo]);
    return pillarPoints_[lo] + w * (pillarPoints_[hi] - pillarPoints_[lo]);
}

double SwapPointsCurve::impliedCarry(const LocalDate& valueDate) const {
    std::int64_t d = LocalDate::daysBetween(spotDate_, valueDate);
    if (d <= 0) {
        throw std::invalid_argument("valueDate must be after spot");
    }
    double tau = static_cast<double>(d) / 365.0;
    return std::log(outright(valueDate) / spotRate_) / tau;
}

}  // namespace quantfinlib
