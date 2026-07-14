#include "quantfinlib/risk/concentration_risk.hpp"

#include <algorithm>
#include <cmath>

namespace quantfinlib {
namespace concentration_risk {

double herfindahlIndex(const std::vector<double>& exposures) {
    double total = 0;
    for (double e : exposures) {
        total += std::fabs(e);
    }
    if (total == 0) {
        return 0;
    }
    double hhi = 0;
    for (double e : exposures) {
        double share = std::fabs(e) / total;
        hhi += share * share;
    }
    return hhi;
}

double effectivePositions(const std::vector<double>& exposures) {
    double hhi = herfindahlIndex(exposures);
    return hhi == 0 ? 0 : 1 / hhi;
}

double topNShare(const std::vector<double>& exposures, int n) {
    double total = 0;
    std::vector<double> abs(exposures.size());
    for (std::size_t i = 0; i < exposures.size(); i++) {
        abs[i] = std::fabs(exposures[i]);
        total += abs[i];
    }
    if (total == 0) {
        return 0;
    }
    std::sort(abs.begin(), abs.end());
    double top = 0;
    int len = static_cast<int>(abs.size());
    for (int i = len - 1; i >= std::max(0, len - n); i--) {
        top += abs[i];
    }
    return top / total;
}

GroupedExposures shares(const GroupedExposures& exposureByGroup) {
    double total = 0;
    for (const auto& e : exposureByGroup) {
        total += std::fabs(e.second);
    }
    GroupedExposures out;
    out.reserve(exposureByGroup.size());
    for (const auto& e : exposureByGroup) {
        out.emplace_back(e.first, total == 0 ? 0 : std::fabs(e.second) / total);
    }
    return out;
}

std::vector<std::string> limitBreaches(const GroupedExposures& exposureByGroup,
                                       double maxShare) {
    std::vector<std::string> out;
    for (const auto& e : shares(exposureByGroup)) {
        if (e.second > maxShare) {
            out.push_back(e.first);
        }
    }
    return out;
}

}  // namespace concentration_risk
}  // namespace quantfinlib
