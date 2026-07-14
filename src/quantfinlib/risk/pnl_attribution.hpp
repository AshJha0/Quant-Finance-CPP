// Port of com.quantfinlib.risk.PnlAttribution (Java): FRTB P&L
// attribution test (PLAT) per MAR32 — Spearman correlation between
// daily HPL and RTPL plus the two-sample Kolmogorov-Smirnov statistic.
// Zones: GREEN (corr > 0.80 and KS < 0.09), RED (corr < 0.70 or
// KS > 0.12), AMBER between. The KS statistic evaluates the CDF gap
// only after BOTH samples have consumed each distinct value (ties must
// not register a transient gap; identical series score exactly 0).
// Styled after BCBS MAR32, not certified.
#pragma once

#include <vector>

namespace quantfinlib {
namespace pnl_attribution {

enum class Zone { GREEN, AMBER, RED };

/** The PLAT verdict for one desk over one window. */
struct Result {
    double spearmanCorrelation;
    double ksStatistic;
    Zone zone;
};

/**
 * Runs the PLAT over aligned daily P&L series (250 days is the
 * regulatory window; anything >= 20 computes).
 *
 * @param hypotheticalPnl    HPL — actual book, actual prices
 * @param riskTheoreticalPnl RTPL — the risk model's factors + pricers
 */
Result test(const std::vector<double>& hypotheticalPnl,
            const std::vector<double>& riskTheoreticalPnl);

/**
 * The two-sample KS statistic: max gap between the empirical CDFs,
 * evaluated after BOTH samples have consumed each distinct value.
 * Non-finite entries throw std::invalid_argument (a NaN would freeze
 * the tie-consuming advance into an infinite loop).
 */
double ksStatistic(const std::vector<double>& a, const std::vector<double>& b);

}  // namespace pnl_attribution
}  // namespace quantfinlib
