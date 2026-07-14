#include "quantfinlib/microstructure/spread_forecaster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

SpreadForecaster::SpreadForecaster(int buckets, double dayAlpha, std::int64_t deviationHalfLifeNanos)
        : buckets_(buckets), dayAlpha_(dayAlpha), deviationHalfLifeNanos_(deviationHalfLifeNanos),
          lastSpread_(std::numeric_limits<double>::quiet_NaN()) {
    if (buckets < 1 || dayAlpha <= 0 || dayAlpha > 1 || deviationHalfLifeNanos <= 0) {
        throw std::invalid_argument("need buckets >= 1, dayAlpha in (0,1], half-life > 0");
    }
    baselineEwma_.assign(static_cast<std::size_t>(buckets), 0.0);
    todaySum_.assign(static_cast<std::size_t>(buckets), 0.0);
    todayCount_.assign(static_cast<std::size_t>(buckets), 0);
}

SpreadForecaster& SpreadForecaster::seedBaseline(const std::vector<double>& spreadPerBucket) {
    if (static_cast<int>(spreadPerBucket.size()) != buckets_) {
        throw std::invalid_argument("baseline length must equal bucket count");
    }
    baselineEwma_ = spreadPerBucket;
    daysLearned_ = 1;
    return *this;
}

void SpreadForecaster::onSpread(int bucket, double spread, std::int64_t timestampNanos) {
    double inf = std::numeric_limits<double>::infinity();
    if (!(spread >= 0) || spread == inf) {
        return;
    }
    lastSpread_ = spread;
    auto idx = static_cast<std::size_t>(bucket);
    todaySum_[idx] += spread;
    todayCount_[idx]++;
    if (daysLearned_ == 0 || baselineEwma_[idx] == 0) {
        return;
    }
    double dev = spread - baselineEwma_[idx];
    double prior = decayedDeviation(timestampNanos);
    deviation_ = hasDeviation_ ? prior + DEVIATION_ALPHA * (dev - prior) : dev;
    hasDeviation_ = true;
    deviationTime_ = timestampNanos;
}

void SpreadForecaster::rollDay() {
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
    deviation_ = 0;
    hasDeviation_ = false;
}

double SpreadForecaster::forecast(int bucket, std::int64_t nowNanos) const {
    if (daysLearned_ == 0) {
        return lastSpread_;
    }
    return std::max(0.0, baselineEwma_[static_cast<std::size_t>(bucket)] + decayedDeviation(nowNanos));
}

double SpreadForecaster::decayedDeviation(std::int64_t now) const {
    if (!hasDeviation_) {
        return 0;
    }
    return deviation_ * math_utils::decayFactor(now - deviationTime_, deviationHalfLifeNanos_);
}

}  // namespace quantfinlib
