#include "quantfinlib/risk/tail_risk.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace tail_risk {

namespace {

constexpr double kSnap = 1e-9;

double snap(double x) {
    double r = std::nearbyint(x);
    return std::fabs(x - r) <= kSnap * std::max(1.0, std::fabs(x)) ? r : x;
}

void requireFinite(const std::vector<double>& losses) {
    for (double v : losses) {
        if (!std::isfinite(v)) {
            throw std::invalid_argument("loss sample contains " + std::to_string(v));
        }
    }
}

}  // namespace

Tail of(const std::vector<double>& losses, double confidence) {
    requireFinite(losses);  // NaN breaks the ordering std::sort needs
    std::vector<double> sorted = losses;
    std::sort(sorted.begin(), sorted.end());
    return ofSorted(sorted, confidence);
}

Tail ofSorted(const std::vector<double>& sorted, double confidence) {
    if (!(confidence > 0 && confidence < 1)) {
        throw std::invalid_argument("confidence must be in (0, 1): "
                                    + std::to_string(confidence));
    }
    if (sorted.empty()) {
        throw std::invalid_argument("empty loss sample");
    }
    requireFinite(sorted);
    int n = static_cast<int>(sorted.size());
    int rank = static_cast<int>(std::ceil(snap(confidence * n)));  // 1-based
    rank = std::max(1, std::min(n, rank));
    double var = sorted[rank - 1];
    double w = snap((1 - confidence) * n);
    if (w <= 1) {
        return {var, sorted[n - 1]};
    }
    int k = static_cast<int>(std::floor(w));
    double sum = 0;
    for (int i = n - k; i < n; i++) {
        sum += sorted[i];
    }
    double frac = w - k;
    if (frac > 0) {
        sum += frac * sorted[n - k - 1];
    }
    return {var, sum / w};
}

}  // namespace tail_risk
}  // namespace quantfinlib
