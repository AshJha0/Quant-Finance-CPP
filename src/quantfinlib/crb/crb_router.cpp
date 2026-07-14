#include "quantfinlib/crb/crb_router.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace quantfinlib {
namespace crb_router {

DarkVenue::DarkVenue(std::string name_, double expectedLiquidity_, double fillProbability_,
                     double adverseSelectionBps_)
    : name(std::move(name_)), expectedLiquidity(expectedLiquidity_),
      fillProbability(fillProbability_), adverseSelectionBps(adverseSelectionBps_) {
    if (name.empty()) {
        throw std::invalid_argument("venue must be named");
    }
    if (!(expectedLiquidity >= 0) || expectedLiquidity == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("expectedLiquidity must be >= 0 and finite");
    }
    if (!(fillProbability >= 0 && fillProbability <= 1)) {
        throw std::invalid_argument("fillProbability must be in [0, 1]");
    }
    if (!std::isfinite(adverseSelectionBps) || adverseSelectionBps < 0) {
        throw std::invalid_argument("adverseSelectionBps must be >= 0 and finite");
    }
}

Allocation route(double notional, double crossableInternal, const std::vector<DarkVenue>& venues,
                 double halfSpreadBps, double impactBps) {
    if (!(notional > 0) || notional == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("notional must be positive and finite");
    }
    if (!(crossableInternal >= 0) || crossableInternal == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("crossableInternal must be >= 0 and finite");
    }
    if (!(halfSpreadBps > 0) || halfSpreadBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("halfSpreadBps must be positive and finite");
    }
    if (!(impactBps >= 0) || impactBps == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("impactBps must be >= 0 and finite");
    }
    double litCost = halfSpreadBps + impactBps;

    double remaining = notional;
    double internalNotional = std::min(remaining, crossableInternal);
    remaining -= internalNotional;
    double costWeighted = 0;

    int m = static_cast<int>(venues.size());
    std::vector<double> dark(m, 0.0);
    std::vector<int> order(m);
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return venues[a].adverseSelectionBps < venues[b].adverseSelectionBps;
    });
    for (int k = 0; k < m && remaining > 0; k++) {
        const DarkVenue& v = venues[order[k]];
        if (v.adverseSelectionBps >= litCost) {
            break;  // ordered: the rest are worse
        }
        double expectedFill = std::min(remaining, v.expectedLiquidity * v.fillProbability);
        if (expectedFill <= 0) {
            continue;
        }
        dark[order[k]] = expectedFill;
        costWeighted += expectedFill * v.adverseSelectionBps;
        remaining -= expectedFill;
    }

    double lit = remaining;
    costWeighted += lit * litCost;
    return {internalNotional, dark, lit, costWeighted / notional};
}

}  // namespace crb_router
}  // namespace quantfinlib
