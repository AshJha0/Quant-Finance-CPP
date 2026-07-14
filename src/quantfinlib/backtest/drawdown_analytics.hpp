// Port of com.quantfinlib.backtest.DrawdownAnalytics: drawdown STRUCTURE
// (depth, duration, time under water, chronological episodes), because
// "max drawdown 18%" hides how long the pain lasted. An episode still
// open at series end reports recoveryIndex = -1 and its duration runs to
// the last bar. Equity must be >= 2 points, finite and > 0 (a
// ratio-of-peak drawdown is meaningless through zero/negative equity);
// violations throw std::invalid_argument.
#pragma once

#include <vector>

namespace quantfinlib {
namespace drawdown_analytics {

/** One peak-to-recovery episode; recoveryIndex == -1 while still open. */
struct Drawdown {
    int peakIndex = 0;
    int troughIndex = 0;
    int recoveryIndex = -1;
    double depth = 0;    // 1 - trough/peak

    /** Periods from peak to recovery, or to series end if open. */
    int duration(int seriesLength) const {
        return (recoveryIndex >= 0 ? recoveryIndex : seriesLength - 1) - peakIndex;
    }
};

struct Result {
    double maxDepth = 0;        // deepest episode's depth (0 if never dips)
    int maxDuration = 0;        // longest episode duration in periods
    double timeUnderWater = 0;  // fraction of periods below the running peak
    std::vector<Drawdown> episodes;  // chronological
};

/** @param equity equity curve, >= 2 points, all finite and > 0. */
Result analyze(const std::vector<double>& equity);

}  // namespace drawdown_analytics
}  // namespace quantfinlib
