// Port of com.quantfinlib.risk.ConcentrationRisk (Java): concentration
// metrics over exposures — Herfindahl-Hirschman index, effective number
// of positions, top-N share, and single-name limit breaches. Java's
// LinkedHashMap (insertion-ordered) maps to a vector of (key, value)
// pairs so grouped results keep insertion order.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace quantfinlib {
namespace concentration_risk {

using GroupedExposures = std::vector<std::pair<std::string, double>>;

/** Herfindahl-Hirschman index of |exposure| shares; 1/N (diversified) .. 1 (single name). */
double herfindahlIndex(const std::vector<double>& exposures);

/** Effective number of equally-weighted positions: 1 / HHI. */
double effectivePositions(const std::vector<double>& exposures);

/** Combined |exposure| share of the largest n positions. */
double topNShare(const std::vector<double>& exposures, int n);

/** |Exposure| share per group key (insertion order preserved). */
GroupedExposures shares(const GroupedExposures& exposureByGroup);

/** Group keys whose share exceeds the single-name concentration limit. */
std::vector<std::string> limitBreaches(const GroupedExposures& exposureByGroup,
                                       double maxShare);

}  // namespace concentration_risk
}  // namespace quantfinlib
