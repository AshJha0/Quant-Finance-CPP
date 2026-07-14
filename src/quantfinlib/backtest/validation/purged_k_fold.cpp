#include "quantfinlib/backtest/validation/purged_k_fold.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace purged_k_fold {

std::vector<Split> splits(int n, int k, int labelHorizon, int embargo) {
    if (k < 2) {
        throw std::invalid_argument("k must be >= 2, got " + std::to_string(k));
    }
    if (n < 2 * k) {
        throw std::invalid_argument("need n >= 2k samples: n=" + std::to_string(n)
                                    + " k=" + std::to_string(k));
    }
    if (labelHorizon < 0 || embargo < 0) {
        throw std::invalid_argument("labelHorizon and embargo must be >= 0, got "
                + std::to_string(labelHorizon) + " and " + std::to_string(embargo));
    }
    std::vector<Split> out;
    out.reserve(static_cast<std::size_t>(k));
    for (int f = 0; f < k; f++) {
        int t0 = static_cast<int>(static_cast<std::int64_t>(f) * n / k);
        int t1 = static_cast<int>(static_cast<std::int64_t>(f + 1) * n / k);
        int headEnd = std::max(0, t0 - labelHorizon);              // [0, headEnd)
        int tailStart = std::min(n, t1 + labelHorizon + embargo);  // [tailStart, n)
        int size = headEnd + (n - tailStart);
        if (size == 0) {
            throw std::invalid_argument("fold " + std::to_string(f)
                    + " leaves no training data: n=" + std::to_string(n)
                    + " k=" + std::to_string(k)
                    + " labelHorizon=" + std::to_string(labelHorizon)
                    + " embargo=" + std::to_string(embargo));
        }
        Split s;
        s.fold = f;
        s.testFrom = t0;
        s.testTo = t1;
        s.trainIndices.reserve(static_cast<std::size_t>(size));
        for (int i = 0; i < headEnd; i++) {
            s.trainIndices.push_back(i);
        }
        for (int i = tailStart; i < n; i++) {
            s.trainIndices.push_back(i);
        }
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace purged_k_fold
}  // namespace quantfinlib
