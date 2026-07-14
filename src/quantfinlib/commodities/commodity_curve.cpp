#include "quantfinlib/commodities/commodity_curve.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {

CommodityCurve CommodityCurve::of(double spot, const std::vector<double>& tenorYears,
                                  const std::vector<double>& prices) {
    constexpr double kInf = std::numeric_limits<double>::infinity();
    if (!(spot > 0) || spot == kInf) {
        throw std::invalid_argument("spot must be positive and finite, got "
                                    + std::to_string(spot));
    }
    std::size_t n = tenorYears.size();
    if (n == 0 || prices.size() != n) {
        throw std::invalid_argument("need aligned, non-empty tenors/prices");
    }
    CommodityCurve c(spot);
    double prev = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (!(tenorYears[i] > prev) || tenorYears[i] == kInf) {
            throw std::invalid_argument("tenors must be ascending, positive, finite");
        }
        if (!(prices[i] > 0) || prices[i] == kInf) {
            throw std::invalid_argument("price must be positive and finite: "
                                        + std::to_string(prices[i]));
        }
        c.futures_[tenorYears[i]] = prices[i];
        prev = tenorYears[i];
    }
    return c;
}

double CommodityCurve::price(double tenorYears) const {
    // Java floorEntry/ceilingEntry semantics; exact pillar returned as-is.
    auto hi = futures_.lower_bound(tenorYears);          // first key >= tenor
    if (hi != futures_.end() && hi->first == tenorYears) {
        return hi->second;
    }
    if (hi == futures_.begin() || hi == futures_.end()) {
        throw std::invalid_argument("tenor " + std::to_string(tenorYears)
                + " outside pillars [" + std::to_string(futures_.begin()->first)
                + ", " + std::to_string(futures_.rbegin()->first) + "]");
    }
    auto lo = std::prev(hi);
    double w = (tenorYears - lo->first) / (hi->first - lo->first);
    return lo->second + w * (hi->second - lo->second);
}

double CommodityCurve::annualizedRollYield(double nearYears, double farYears) const {
    if (!(farYears > nearYears)) {
        throw std::invalid_argument("farYears must exceed nearYears");
    }
    return std::log(price(nearYears) / price(farYears)) / (farYears - nearYears);
}

double CommodityCurve::impliedCarry(double tenorYears, double rate) const {
    if (!std::isfinite(rate)) {
        throw std::invalid_argument("rate must be finite");
    }
    return std::log(price(tenorYears) / spot_) / tenorYears - rate;
}

bool CommodityCurve::isContango() const {
    double prev = spot_;
    for (const auto& [tenor, px] : futures_) {
        (void)tenor;
        if (px <= prev) {
            return false;
        }
        prev = px;
    }
    return true;
}

bool CommodityCurve::isBackwardation() const {
    double prev = spot_;
    for (const auto& [tenor, px] : futures_) {
        (void)tenor;
        if (px >= prev) {
            return false;
        }
        prev = px;
    }
    return true;
}

}  // namespace quantfinlib
