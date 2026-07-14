#include "quantfinlib/microstructure/volatility_curve.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

VolatilityCurve::VolatilityCurve(int buckets, double dayAlpha) : buckets_(buckets), dayAlpha_(dayAlpha) {
    if (buckets < 1 || dayAlpha <= 0 || dayAlpha > 1) {
        throw std::invalid_argument("need buckets >= 1, dayAlpha in (0,1]");
    }
    baselineEwma_.assign(static_cast<std::size_t>(buckets), 0.0);
    todaySum_.assign(static_cast<std::size_t>(buckets), 0.0);
    todayCount_.assign(static_cast<std::size_t>(buckets), 0);
}

VolatilityCurve& VolatilityCurve::seedBaseline(const std::vector<double>& volPerBucket) {
    if (static_cast<int>(volPerBucket.size()) != buckets_) {
        throw std::invalid_argument("baseline length must equal bucket count");
    }
    baselineEwma_ = volPerBucket;
    daysLearned_ = 1;
    return *this;
}

void VolatilityCurve::onVol(int bucket, double volPerSqrtSecond) {
    double inf = std::numeric_limits<double>::infinity();
    if (!(volPerSqrtSecond >= 0) || volPerSqrtSecond == inf) {
        return;
    }
    todaySum_[static_cast<std::size_t>(bucket)] += volPerSqrtSecond;
    todayCount_[static_cast<std::size_t>(bucket)]++;
}

void VolatilityCurve::rollDay() {
    for (int b = 0; b < buckets_; b++) {
        auto idx = static_cast<std::size_t>(b);
        if (todayCount_[idx] > 0) {
            double todayMean = todaySum_[idx] / static_cast<double>(todayCount_[idx]);
            baselineEwma_[idx] = baselineEwma_[idx] == 0
                    ? todayMean
                    : baselineEwma_[idx] + dayAlpha_ * (todayMean - baselineEwma_[idx]);
        }
        todaySum_[idx] = 0;
        todayCount_[idx] = 0;
    }
    daysLearned_++;
}

double VolatilityCurve::regime(int bucket, double currentVolPerSqrtSecond) const {
    double base = baselineEwma_[static_cast<std::size_t>(bucket)];
    double inf = std::numeric_limits<double>::infinity();
    if (base <= 0 || !(currentVolPerSqrtSecond >= 0) || currentVolPerSqrtSecond == inf) {
        return 0;
    }
    return math_utils::clamp(currentVolPerSqrtSecond / base - 1, 0, 1);
}

}  // namespace quantfinlib
