#include "quantfinlib/backtest/validation/monte_carlo_trade_shuffle.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <utility>

#include "quantfinlib/backtest/validation/splittable_random.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace monte_carlo_trade_shuffle {

namespace {
/** Max peak-to-trough drop on the cumulative-P&L path (>= 0). */
double maxDrawdown(const std::vector<double>& pnls) {
    double cum = 0, peak = 0, maxDd = 0;
    for (double p : pnls) {
        cum += p;
        peak = std::max(peak, cum);
        maxDd = std::max(maxDd, peak - cum);
    }
    return maxDd;
}
}  // namespace

Result analyze(const std::vector<Trade>& trades, int shuffles, std::int64_t seed) {
    int n = static_cast<int>(trades.size());
    if (n < 2) {
        throw std::invalid_argument("need >= 2 trades, got " + std::to_string(n));
    }
    if (shuffles < 100) {
        throw std::invalid_argument("need >= 100 shuffles, got " + std::to_string(shuffles));
    }
    std::vector<double> pnls(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        double p = trades[static_cast<std::size_t>(i)].pnl;
        if (!std::isfinite(p)) {
            throw std::invalid_argument("non-finite trade pnl at " + std::to_string(i));
        }
        pnls[static_cast<std::size_t>(i)] = p;
    }

    double actualDd = maxDrawdown(pnls);
    std::vector<double> dd(static_cast<std::size_t>(shuffles));
    std::vector<double> terminal(static_cast<std::size_t>(shuffles));
    int losses = 0;
    int actualAtOrBelow = 0;
    SplittableRandom rnd(seed);
    std::vector<double> work(static_cast<std::size_t>(n));
    for (int s = 0; s < shuffles; s++) {
        std::copy(pnls.begin(), pnls.end(), work.begin());
        // Fisher-Yates.
        for (int i = n - 1; i > 0; i--) {
            int j = rnd.nextInt(i + 1);
            std::swap(work[static_cast<std::size_t>(i)], work[static_cast<std::size_t>(j)]);
        }
        dd[static_cast<std::size_t>(s)] = maxDrawdown(work);
        double sum = 0;
        for (double p : work) {
            sum += p;
        }
        terminal[static_cast<std::size_t>(s)] = sum;
        if (sum < 0) {
            losses++;
        }
        if (dd[static_cast<std::size_t>(s)] <= actualDd) {
            actualAtOrBelow++;
        }
    }
    Result r;
    r.medianMaxDrawdown = math_utils::percentile(dd, 0.50);
    r.p95MaxDrawdown = math_utils::percentile(dd, 0.95);
    r.p99MaxDrawdown = math_utils::percentile(dd, 0.99);
    r.medianTerminalPnl = math_utils::percentile(terminal, 0.50);
    r.probLoss = static_cast<double>(losses) / shuffles;
    r.actualMaxDrawdown = actualDd;
    r.actualDrawdownPct = static_cast<double>(actualAtOrBelow) / shuffles;
    return r;
}

}  // namespace monte_carlo_trade_shuffle
}  // namespace quantfinlib
