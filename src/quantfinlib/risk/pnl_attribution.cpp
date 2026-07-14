#include "quantfinlib/risk/pnl_attribution.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/risk/dependence.hpp"

namespace quantfinlib {
namespace pnl_attribution {

Result test(const std::vector<double>& hypotheticalPnl,
            const std::vector<double>& riskTheoreticalPnl) {
    if (hypotheticalPnl.size() != riskTheoreticalPnl.size()
            || hypotheticalPnl.size() < 20) {
        throw std::invalid_argument("need aligned series of >= 20 days");
    }
    double corr = dependence::spearman(hypotheticalPnl, riskTheoreticalPnl);
    double ks = ksStatistic(hypotheticalPnl, riskTheoreticalPnl);
    Zone zone;
    if (corr < 0.70 || ks > 0.12) {
        zone = Zone::RED;
    } else if (corr > 0.80 && ks < 0.09) {
        zone = Zone::GREEN;
    } else {
        zone = Zone::AMBER;
    }
    return {corr, ks, zone};
}

double ksStatistic(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.empty() || b.empty()) {
        throw std::invalid_argument("need non-empty samples");
    }
    // A NaN would freeze the tie-consuming advance below (NaN == NaN is
    // false — neither index ever moves), turning one missing P&L day
    // into an INFINITE LOOP. Java sorts (NaN last) and checks the ends;
    // std::sort with NaN is undefined behavior, so the C++ port gates
    // BEFORE sorting — same inputs rejected, same exception.
    for (double v : a) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument("P&L series must be finite");
        }
    }
    for (double v : b) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument("P&L series must be finite");
        }
    }
    std::vector<double> sa = a;
    std::vector<double> sb = b;
    std::sort(sa.begin(), sa.end());
    std::sort(sb.begin(), sb.end());
    std::size_t i = 0;
    std::size_t j = 0;
    double maxGap = 0;
    while (i < sa.size() && j < sb.size()) {
        double v = std::min(sa[i], sb[j]);
        while (i < sa.size() && sa[i] == v) {
            i++;
        }
        while (j < sb.size() && sb[j] == v) {
            j++;
        }
        double gap = std::fabs(static_cast<double>(i) / sa.size()
                - static_cast<double>(j) / sb.size());
        if (gap > maxGap) {
            maxGap = gap;
        }
    }
    return maxGap;
}

}  // namespace pnl_attribution
}  // namespace quantfinlib
