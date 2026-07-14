#include "quantfinlib/backtest/drawdown_analytics.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace drawdown_analytics {

Result analyze(const std::vector<double>& equity) {
    int n = static_cast<int>(equity.size());
    if (n < 2) {
        throw std::invalid_argument("need >= 2 equity points, got " + std::to_string(n));
    }
    for (double e : equity) {
        // !(e > 0) also rejects NaN, matching the Java idiom.
        if (!(e > 0) || e == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("equity must be finite and > 0, got "
                                        + std::to_string(e));
        }
    }
    std::vector<Drawdown> episodes;
    double peak = equity[0];
    int peakIdx = 0;
    int underWater = 0;
    bool open = false;
    double trough = 0;
    int troughIdx = -1;

    for (int i = 1; i < n; i++) {
        if (equity[i] >= peak) {
            if (open) {
                episodes.push_back({peakIdx, troughIdx, i, 1 - trough / peak});
                open = false;
            }
            peak = equity[i];
            peakIdx = i;
        } else {
            underWater++;
            if (!open) {
                open = true;
                trough = equity[i];
                troughIdx = i;
            } else if (equity[i] < trough) {
                trough = equity[i];
                troughIdx = i;
            }
        }
    }
    if (open) {
        episodes.push_back({peakIdx, troughIdx, -1, 1 - trough / peak});
    }

    double maxDepth = 0;
    int maxDuration = 0;
    for (const Drawdown& d : episodes) {
        maxDepth = std::max(maxDepth, d.depth);
        maxDuration = std::max(maxDuration, d.duration(n));
    }
    Result r;
    r.maxDepth = maxDepth;
    r.maxDuration = maxDuration;
    r.timeUnderWater = static_cast<double>(underWater) / n;
    r.episodes = std::move(episodes);
    return r;
}

}  // namespace drawdown_analytics
}  // namespace quantfinlib
