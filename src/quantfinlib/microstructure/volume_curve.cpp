#include "quantfinlib/microstructure/volume_curve.hpp"

#include <algorithm>
#include <stdexcept>

namespace quantfinlib {

VolumeCurve::VolumeCurve(int buckets, double alpha) : buckets_(buckets), alpha_(alpha) {
    if (buckets < 1 || alpha <= 0 || alpha > 1) {
        throw std::invalid_argument("need buckets >= 1, alpha in (0,1]");
    }
    profileEwma_.assign(static_cast<std::size_t>(buckets), 0.0);
    cumProfile_.assign(static_cast<std::size_t>(buckets), 0.0);
    today_.assign(static_cast<std::size_t>(buckets), 0.0);
}

VolumeCurve& VolumeCurve::seedProfile(const std::vector<double>& volumesPerBucket) {
    if (static_cast<int>(volumesPerBucket.size()) != buckets_) {
        throw std::invalid_argument("profile length must equal bucket count");
    }
    for (int b = 0; b < buckets_; b++) {
        if (volumesPerBucket[static_cast<std::size_t>(b)] < 0) {
            throw std::invalid_argument("negative volume in profile");
        }
        profileEwma_[static_cast<std::size_t>(b)] = volumesPerBucket[static_cast<std::size_t>(b)];
    }
    rebuildPrefixSums();
    daysLearned_ = 1;
    return *this;
}

void VolumeCurve::rebuildPrefixSums() {
    double cum = 0;
    for (int b = 0; b < buckets_; b++) {
        cum += profileEwma_[static_cast<std::size_t>(b)];
        cumProfile_[static_cast<std::size_t>(b)] = cum;
    }
    profileTotal_ = cum;
}

void VolumeCurve::onVolume(int bucket, std::int64_t qty) {
    if (qty > 0) {
        today_[static_cast<std::size_t>(bucket)] += static_cast<double>(qty);
        todayTotal_ += static_cast<double>(qty);
    }
}

void VolumeCurve::rollDay() {
    for (int b = 0; b < buckets_; b++) {
        auto idx = static_cast<std::size_t>(b);
        profileEwma_[idx] = daysLearned_ == 0
                ? today_[idx]
                : profileEwma_[idx] + alpha_ * (today_[idx] - profileEwma_[idx]);
        today_[idx] = 0;
    }
    rebuildPrefixSums();
    todayTotal_ = 0;
    daysLearned_++;
}

double VolumeCurve::expectedFractionElapsed(int bucket, double fracWithinBucket) const {
    double f = std::max(0.0, std::min(1.0, fracWithinBucket));
    if (profileTotal_ <= 0) {
        return (bucket + f) / buckets_;
    }
    double cum = (bucket > 0 ? cumProfile_[static_cast<std::size_t>(bucket - 1)] : 0)
            + f * profileEwma_[static_cast<std::size_t>(bucket)];
    return std::min(1.0, cum / profileTotal_);
}

double VolumeCurve::projectedDayVolume(int bucket, double fracWithinBucket) const {
    if (profileTotal_ <= 0) {
        return realizedToday();
    }
    double expectedSoFar = expectedFractionElapsed(bucket, fracWithinBucket) * profileTotal_;
    double realized = realizedToday();
    if (expectedSoFar <= 0) {
        return profileTotal_;
    }
    double ratio = realized / expectedSoFar;
    double w = std::min(1.0, expectedSoFar / profileTotal_);
    double scale = 1 + w * (ratio - 1);
    return profileTotal_ * std::max(0.0, scale);
}

double VolumeCurve::expectedVolumeRemaining(int bucket, double fracWithinBucket) const {
    return std::max(0.0, projectedDayVolume(bucket, fracWithinBucket) - realizedToday());
}

void VolumeCurve::writeState(BinWriter& out) const {
    out.writeByte(1);
    out.writeInt(daysLearned_);
    Checkpoint::writeDoubles(out, profileEwma_);
}

void VolumeCurve::readState(BinReader& in) {
    Checkpoint::requireVersion(in, 1, "VolumeCurve");
    std::int32_t days = in.readInt();
    Checkpoint::readDoublesInto(in, profileEwma_);
    daysLearned_ = days;
    rebuildPrefixSums();
    std::fill(today_.begin(), today_.end(), 0.0);
    todayTotal_ = 0;
}

}  // namespace quantfinlib
