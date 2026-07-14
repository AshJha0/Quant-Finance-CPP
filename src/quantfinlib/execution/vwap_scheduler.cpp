#include "quantfinlib/execution/vwap_scheduler.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>

namespace quantfinlib {
namespace vwap_scheduler {

std::vector<std::int64_t> allocateProportionally(std::int64_t total, const std::vector<double>& weights) {
    std::size_t n = weights.size();
    double sum = 0;
    for (double w : weights) {
        if (w < 0) {
            throw std::invalid_argument("negative weight: " + std::to_string(w));
        }
        sum += w;
    }
    std::vector<std::int64_t> out(n, 0);
    if (sum == 0) {
        out[0] = total;
        return out;
    }
    std::vector<double> fractions(n, 0.0);
    std::int64_t allocated = 0;
    for (std::size_t i = 0; i < n; i++) {
        double raw = static_cast<double>(total) * weights[i] / sum;
        out[i] = static_cast<std::int64_t>(std::floor(raw));
        fractions[i] = raw - static_cast<double>(out[i]);
        allocated += out[i];
    }
    // Distribute the remainder to the largest fractional parts (stable
    // sort: ties keep ascending-index order, matching Java's stable
    // Arrays.sort with a Comparator).
    std::int64_t remainder = total - allocated;
    std::vector<std::size_t> order(n);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return fractions[a] > fractions[b]; });
    for (std::int64_t k = 0; k < remainder; k++) {
        out[order[static_cast<std::size_t>(k) % n]]++;
    }
    return out;
}

std::vector<Slice> schedule(std::int64_t totalQty, const std::vector<double>& volumeProfile,
                            std::int64_t durationMillis) {
    if (volumeProfile.empty() || totalQty <= 0) {
        throw std::invalid_argument("need positive quantity and a non-empty profile");
    }
    auto n = static_cast<std::int64_t>(volumeProfile.size());
    if (durationMillis < 0 || durationMillis > std::numeric_limits<std::int64_t>::max() / n) {
        throw std::invalid_argument("durationMillis out of range for " + std::to_string(n) + " slices");
    }
    std::vector<std::int64_t> quantities = allocateProportionally(totalQty, volumeProfile);
    std::vector<Slice> out;
    out.reserve(volumeProfile.size());
    for (std::int64_t i = 0; i < n; i++) {
        out.push_back(Slice{durationMillis * i / n, quantities[static_cast<std::size_t>(i)]});
    }
    return out;
}

}  // namespace vwap_scheduler
}  // namespace quantfinlib
