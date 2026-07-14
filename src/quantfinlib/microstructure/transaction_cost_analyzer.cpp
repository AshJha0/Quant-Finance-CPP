#include "quantfinlib/microstructure/transaction_cost_analyzer.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace transaction_cost_analyzer {

TcaReport analyze(const std::vector<Execution>& fills, double arrivalMid, double marketVwap,
                  const std::vector<double>& midAtFill) {
    if (fills.empty()) {
        throw std::invalid_argument("no fills");
    }
    if (midAtFill.size() != fills.size()) {
        throw std::invalid_argument("midAtFill must align with fills");
    }
    // Benchmark prices divide every headline number: a single stale
    // zero mid would put an Infinity in the report with no warning.
    if (!(arrivalMid > 0) || !std::isfinite(arrivalMid) || !(marketVwap > 0)
        || !std::isfinite(marketVwap)) {
        throw std::invalid_argument("arrivalMid and marketVwap must be positive and finite: "
                                    + std::to_string(arrivalMid) + ", "
                                    + std::to_string(marketVwap));
    }
    for (std::size_t i = 0; i < midAtFill.size(); i++) {
        if (!(midAtFill[i] > 0) || !std::isfinite(midAtFill[i])) {
            throw std::invalid_argument("midAtFill[" + std::to_string(i)
                                        + "] must be positive and finite: "
                                        + std::to_string(midAtFill[i]));
        }
    }
    int sgn = sign(fills.front().side);

    std::int64_t qty = 0;
    double notional = 0;
    double effSpreadWeighted = 0;
    for (std::size_t i = 0; i < fills.size(); i++) {
        const Execution& f = fills[i];
        qty += f.quantity;
        notional += f.notional();
        effSpreadWeighted += 2.0 * sgn * (f.price - midAtFill[i]) / midAtFill[i] * 1e4
                             * static_cast<double>(f.quantity);
    }
    double vwapExec = notional / static_cast<double>(qty);

    TcaReport report;
    report.totalQuantity = qty;
    report.avgExecutionPrice = vwapExec;
    report.arrivalMid = arrivalMid;
    report.marketVwap = marketVwap;
    report.implementationShortfallBps = sgn * (vwapExec - arrivalMid) / arrivalMid * 1e4;
    report.slippageVsVwapBps = sgn * (vwapExec - marketVwap) / marketVwap * 1e4;
    report.avgEffectiveSpreadBps = effSpreadWeighted / static_cast<double>(qty);
    return report;
}

}  // namespace transaction_cost_analyzer
}  // namespace quantfinlib
