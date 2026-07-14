// Port of com.quantfinlib.microstructure.TransactionCostAnalyzer:
// Transaction Cost Analysis benchmarking matched trades against the
// arrival mid, the interval market VWAP, and the prevailing mid at each
// fill (effective spread). All costs are signed so positive = cost to
// the trader, for both buys and sells. Gates map to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/microstructure/execution.hpp"

namespace quantfinlib {
namespace transaction_cost_analyzer {

struct TcaReport {
    std::int64_t totalQuantity = 0;
    double avgExecutionPrice = 0;
    double arrivalMid = 0;
    double marketVwap = 0;
    double implementationShortfallBps = 0;
    double slippageVsVwapBps = 0;
    double avgEffectiveSpreadBps = 0;
};

/**
 * @param fills      the child fills of one parent order (all same side)
 * @param arrivalMid market mid when the parent order was created
 * @param marketVwap market VWAP over the execution interval
 * @param midAtFill  prevailing mid at each fill, aligned with fills
 */
TcaReport analyze(const std::vector<Execution>& fills, double arrivalMid, double marketVwap,
                  const std::vector<double>& midAtFill);

}  // namespace transaction_cost_analyzer
}  // namespace quantfinlib
