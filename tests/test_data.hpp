// Port of com.quantfinlib.TestData (Java): deterministic synthetic data
// generators shared by the test suite. Java uses SplittableRandom; the exact
// stream is not reproducible in C++, so a seeded mt19937 supplies the same
// SHAPE of data (GBM bars with the same drift/vol/decoration structure) and
// tests assert the same properties/tolerances as the Java suite.
#pragma once

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

#include "quantfinlib/data/bar.hpp"

namespace qtestdata {

/** GBM daily series with OHLC structure; deterministic for a given seed. */
inline std::vector<quantfinlib::Bar> gbmBars(int days, double startPrice,
                                             double annualDrift, double annualVol,
                                             unsigned seed) {
    std::mt19937 rnd(seed);
    std::normal_distribution<double> gauss(0.0, 1.0);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    double dt = 1.0 / 252;
    double price = startPrice;
    std::vector<quantfinlib::Bar> bars;
    bars.reserve(days);
    for (int d = 0; d < days; d++) {
        double z = gauss(rnd);
        double next = price * std::exp((annualDrift - 0.5 * annualVol * annualVol) * dt
                                       + annualVol * std::sqrt(dt) * z);
        double high = std::max(price, next) * (1 + uni(rnd) * 0.004);
        double low = std::min(price, next) * (1 - uni(rnd) * 0.004);
        bars.emplace_back(d * 86400000LL, price, high, low, next,
                          1000000 * (0.5 + uni(rnd)));
        price = next;
    }
    return bars;
}

inline std::vector<double> closes(const std::vector<quantfinlib::Bar>& bars) {
    std::vector<double> v(bars.size());
    for (std::size_t i = 0; i < bars.size(); i++) {
        v[i] = bars[i].close();
    }
    return v;
}

}  // namespace qtestdata
