#include "quantfinlib/rates/yield_curve.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {

YieldCurve YieldCurve::ofZeroRates(const std::vector<double>& tenorYears,
                                   const std::vector<double>& zeroRatesCc) {
    if (tenorYears.size() != zeroRatesCc.size() || tenorYears.empty()) {
        throw std::invalid_argument("need equal-length, non-empty tenor/rate arrays");
    }
    YieldCurve c;
    for (std::size_t i = 0; i < tenorYears.size(); i++) {
        if (!(tenorYears[i] > 0)) {
            throw std::invalid_argument("tenor must be positive: "
                                        + std::to_string(tenorYears[i]));
        }
        c.zeros_[tenorYears[i]] = zeroRatesCc[i];
    }
    return c;
}

YieldCurve YieldCurve::bootstrapAnnualParSwaps(const std::vector<int>& tenorYears,
                                               const std::vector<double>& parRates) {
    if (tenorYears.size() != parRates.size() || tenorYears.empty()) {
        throw std::invalid_argument("need equal-length, non-empty tenor/rate arrays");
    }
    int maxYear = tenorYears.back();
    // Interpolate par rates for every year 1..maxYear (Java loop copied 1:1).
    std::vector<double> par(static_cast<std::size_t>(maxYear) + 1, 0.0);
    std::size_t k = 0;
    for (int y = 1; y <= maxYear; y++) {
        while (k < tenorYears.size() - 1 && tenorYears[k + 1] < y) {
            k++;
        }
        if (y <= tenorYears.front()) {
            par[y] = parRates.front();
        } else if (y >= tenorYears.back()) {
            par[y] = parRates.back();
        } else {
            std::size_t lo = k, hi = k + 1;
            double w = static_cast<double>(y - tenorYears[lo])
                       / (tenorYears[hi] - tenorYears[lo]);
            par[y] = parRates[lo] + w * (parRates[hi] - parRates[lo]);
        }
    }
    YieldCurve c;
    double annuity = 0;
    for (int y = 1; y <= maxYear; y++) {
        double df = (1 - par[y] * annuity) / (1 + par[y]);
        if (df <= 0) {
            throw std::invalid_argument("bootstrap produced non-positive DF at year "
                                        + std::to_string(y));
        }
        annuity += df;
        c.zeros_[static_cast<double>(y)] = -std::log(df) / y;
    }
    return c;
}

double YieldCurve::zeroRate(double tenorYears) const {
    // Java floorEntry/ceilingEntry semantics on std::map.
    auto hi = zeros_.lower_bound(tenorYears);            // first key >= tenor (ceiling)
    if (hi != zeros_.end() && hi->first == tenorYears) {
        return hi->second;                               // exact pillar (lo == hi)
    }
    if (hi == zeros_.begin()) {
        return hi->second;                               // no floor: flat short end
    }
    if (hi == zeros_.end()) {
        return std::prev(hi)->second;                    // no ceiling: flat long end
    }
    auto lo = std::prev(hi);
    double w = (tenorYears - lo->first) / (hi->first - lo->first);
    return lo->second + w * (hi->second - lo->second);
}

double YieldCurve::discountFactor(double tenorYears) const {
    if (tenorYears <= 0) {
        return 1;
    }
    return std::exp(-zeroRate(tenorYears) * tenorYears);
}

double YieldCurve::forwardRate(double fromYears, double toYears) const {
    if (toYears <= fromYears) {
        throw std::invalid_argument("toYears must exceed fromYears");
    }
    double z1 = zeroRate(fromYears) * fromYears;
    double z2 = zeroRate(toYears) * toYears;
    return (z2 - z1) / (toYears - fromYears);
}

std::vector<double> YieldCurve::tenors() const {
    std::vector<double> t;
    t.reserve(zeros_.size());
    for (const auto& [tenor, rate] : zeros_) {
        (void)rate;
        t.push_back(tenor);
    }
    return t;
}

}  // namespace quantfinlib
