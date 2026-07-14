#include "quantfinlib/backtest/validation/block_bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "quantfinlib/backtest/risk_helpers.hpp"

namespace quantfinlib {
namespace block_bootstrap {

namespace {

void requireSeries(const std::vector<double>& series, int meanBlockLength) {
    if (series.size() < 50) {
        throw std::invalid_argument("need >= 50 observations");
    }
    if (meanBlockLength < 1) {
        throw std::invalid_argument("meanBlockLength must be >= 1");
    }
    for (double r : series) {
        if (!std::isfinite(r)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
}

void resampleInto(const std::vector<double>& series, int meanBlockLength,
                  SplittableRandom& rnd, std::vector<double>& out) {
    int n = static_cast<int>(series.size());
    double restart = 1.0 / meanBlockLength;
    int i = rnd.nextInt(n);
    for (int t = 0; t < n; t++) {
        out[static_cast<std::size_t>(t)] = series[static_cast<std::size_t>(i)];
        i = rnd.nextDouble() < restart ? rnd.nextInt(n) : (i + 1) % n;
    }
}

}  // namespace

std::vector<double> sharpeSamples(const std::vector<double>& returns, int meanBlockLength,
                                  int resamples, int periodsPerYear, std::int64_t seed) {
    if (resamples < 100) {
        throw std::invalid_argument("need >= 100 resamples for a distribution");
    }
    if (periodsPerYear < 1) {
        throw std::invalid_argument("periodsPerYear must be >= 1");
    }
    SplittableRandom rnd(seed);
    std::vector<double> samples(static_cast<std::size_t>(resamples));
    std::vector<double> path(returns.size());
    requireSeries(returns, meanBlockLength);
    for (int s = 0; s < resamples; s++) {
        resampleInto(returns, meanBlockLength, rnd, path);
        samples[static_cast<std::size_t>(s)] =
                backtest_detail::sharpeRatio(path, 0, periodsPerYear);
    }
    std::sort(samples.begin(), samples.end());
    return samples;
}

std::vector<double> resample(const std::vector<double>& series, int meanBlockLength,
                             SplittableRandom& rnd) {
    requireSeries(series, meanBlockLength);
    std::vector<double> out(series.size());
    resampleInto(series, meanBlockLength, rnd, out);
    return out;
}

}  // namespace block_bootstrap
}  // namespace quantfinlib
