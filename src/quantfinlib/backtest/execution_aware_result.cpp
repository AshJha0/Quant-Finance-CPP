#include "quantfinlib/backtest/execution_aware_result.hpp"

#include <stdexcept>

namespace quantfinlib {

ExecutionAwareResult::ExecutionAwareResult(BacktestResult backtest,
                                           std::vector<ParentOrder> parentOrders,
                                           BarSeries series)
        : backtest_(std::move(backtest)),
          parentOrders_(std::move(parentOrders)),
          series_(std::move(series)) {}

std::vector<Execution> ExecutionAwareResult::allFills() const {
    std::vector<Execution> out;
    for (const ParentOrder& p : parentOrders_) {
        out.insert(out.end(), p.fills.begin(), p.fills.end());
    }
    return out;
}

transaction_cost_analyzer::TcaReport ExecutionAwareResult::tca(const ParentOrder& parent) const {
    std::size_t n = parent.fills.size();
    if (n == 0) {
        throw std::invalid_argument("parent order has no fills");
    }
    std::vector<double> mids(n, 0.0);
    for (std::size_t i = 0; i < n; i++) {
        mids[i] = series_.close(parent.fillBarIndices[i]);
    }
    int first = parent.fillBarIndices.front();
    int last = parent.fillBarIndices.back();
    double pv = 0, vol = 0;
    for (int i = first; i <= last; i++) {
        pv += series_.close(i) * series_.volume(i);
        vol += series_.volume(i);
    }
    double marketVwap = vol == 0 ? parent.arrivalPrice : pv / vol;
    return transaction_cost_analyzer::analyze(parent.fills, parent.arrivalPrice, marketVwap,
                                              mids);
}

}  // namespace quantfinlib
