#include "quantfinlib/risk/dependence.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <numeric>
#include <stdexcept>

#include "quantfinlib/risk/risk_metrics.hpp"

namespace quantfinlib {
namespace dependence {

namespace {

void requireSameLength(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size() || a.size() < 2) {
        throw std::invalid_argument("need two equal-length series of >= 2");
    }
}

}  // namespace

double spearman(const std::vector<double>& a, const std::vector<double>& b) {
    requireSameLength(a, b);
    return risk_metrics::correlation(ranks(a), ranks(b));
}

double kendallTau(const std::vector<double>& a, const std::vector<double>& b) {
    requireSameLength(a, b);
    int n = static_cast<int>(a.size());
    long long concordant = 0;
    long long discordant = 0;
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            double da = a[i] - a[j];
            double db = b[i] - b[j];
            double prod = da * db;
            if (prod > 0) {
                concordant++;
            } else if (prod < 0) {
                discordant++;
            }
            // ties: neither — tau-a convention
        }
    }
    long long pairs = static_cast<long long>(n) * (n - 1) / 2;
    return static_cast<double>(concordant - discordant) / static_cast<double>(pairs);
}

double pearsonFromKendall(double tau) {
    if (!(tau >= -1 && tau <= 1)) {
        throw std::invalid_argument("tau must be in [-1, 1]");
    }
    return std::sin(std::numbers::pi * tau / 2);
}

std::vector<double> ranks(const std::vector<double>& values) {
    int n = static_cast<int>(values.size());
    std::vector<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    // Java sorts Integer[] with a comparator (TimSort, stable); ties get
    // the same midrank so stability does not change the result, but
    // stable_sort mirrors the Java pass exactly.
    std::stable_sort(order.begin(), order.end(),
                     [&values](int x, int y) { return values[x] < values[y]; });
    std::vector<double> rank(n);
    int i = 0;
    while (i < n) {
        int j = i;
        while (j + 1 < n && values[order[j + 1]] == values[order[i]]) {
            j++;
        }
        double mid = (i + j) / 2.0 + 1;  // 1-based midrank
        for (int k = i; k <= j; k++) {
            rank[order[k]] = mid;
        }
        i = j + 1;
    }
    return rank;
}

}  // namespace dependence
}  // namespace quantfinlib
