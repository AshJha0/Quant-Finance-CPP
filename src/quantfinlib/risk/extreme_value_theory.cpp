#include "quantfinlib/risk/extreme_value_theory.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace extreme_value_theory {

double GpdFit::var(double p) const {
    double tailProb = static_cast<double>(exceedances) / sampleSize;
    if (!(p > 1 - tailProb) || !(p < 1)) {
        throw std::invalid_argument("p = " + std::to_string(p)
                + " must lie in (" + std::to_string(1 - tailProb)
                + ", 1) — below that use plain historical VaR; "
                + "99.9% is 0.999, not 99.9");
    }
    double ratio = (1 - p) / tailProb;
    if (std::fabs(shape) < 1e-9) {
        return threshold - scale * std::log(ratio);
    }
    return threshold + scale / shape * (std::pow(ratio, -shape) - 1);
}

double GpdFit::expectedShortfall(double p) const {
    if (shape >= 1) {
        // Java IllegalStateException -> std::runtime_error.
        throw std::runtime_error("shape " + std::to_string(shape)
                + " >= 1: the fitted tail has no finite mean");
    }
    double v = var(p);
    return (v + scale - shape * threshold) / (1 - shape);
}

GpdFit fitPot(const std::vector<double>& losses, double thresholdQuantile) {
    if (losses.size() < 50) {
        throw std::invalid_argument("need >= 50 losses for a tail fit");
    }
    if (!(thresholdQuantile >= 0.5 && thresholdQuantile < 1)) {
        throw std::invalid_argument("thresholdQuantile must be in [0.5, 1)");
    }
    // NaN/Infinity would poison the PWMs with a misleading "degenerate
    // tail" message. Java sorts first (NaN sorts last) and checks the
    // ends; std::sort on NaN is undefined behavior, so the C++ port
    // gates BEFORE sorting — same inputs rejected, same exception.
    for (double x : losses) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument("losses must be finite");
        }
    }
    std::vector<double> sorted = losses;
    std::sort(sorted.begin(), sorted.end());
    int n = static_cast<int>(sorted.size());
    int thresholdIndex = static_cast<int>(std::floor(thresholdQuantile * n)) - 1;
    double u = sorted[std::max(0, thresholdIndex)];

    // Exceedances y_i = loss - u, ascending — STRICTLY above u: on
    // discretized data (P&L snapped to ticks) ties equal to u would
    // otherwise enter as y = 0 exceedances, deflating both PWMs and
    // biasing the shape estimate.
    int start = thresholdIndex + 1;
    while (start < n && sorted[start] <= u) {
        start++;
    }
    int m = n - start;
    if (m < 10) {
        throw std::invalid_argument("only " + std::to_string(m)
                + " exceedances — lower the threshold or bring more data");
    }
    // Probability-weighted moments (Hosking & Wallis 1987):
    // b0 = E[Y] = mean(y);  a1 = E[Y*(1-F(Y))], estimated over the
    // ASCENDING order statistics with weight (m-1-i)/(m-1). Then
    // xi = 2 - b0/(b0 - 2 a1),  beta = 2 b0 a1/(b0 - 2 a1) — for
    // GPD(xi, beta): b0 = beta/(1-xi), a1 = beta/(2(2-xi)), and the
    // algebra inverts exactly.
    double b0 = 0;
    double a1 = 0;
    for (int i = 0; i < m; i++) {
        double y = sorted[start + i] - u;
        b0 += y;
        a1 += y * (m - 1 - i) / static_cast<double>(m - 1);
    }
    b0 /= m;
    a1 /= m;
    double denom = b0 - 2 * a1;
    double shape = 2 - b0 / denom;
    double scale = 2 * b0 * a1 / denom;
    if (!(scale > 0) || !std::isfinite(shape)) {
        throw std::invalid_argument(
                "degenerate tail (all exceedances equal?) — PWM fit failed");
    }
    return {u, shape, scale, m, n};
}

}  // namespace extreme_value_theory
}  // namespace quantfinlib
