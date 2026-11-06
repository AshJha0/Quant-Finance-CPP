#include "quantfinlib/util/latency_recorder.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <limits>

namespace quantfinlib {

int LatencyRecorder::bucketIndex(std::int64_t v) {
    if (v < SUB) {
        return static_cast<int>(v);
    }
    int exp = 63 - std::countl_zero(static_cast<std::uint64_t>(v));
    int sub = static_cast<int>((static_cast<std::uint64_t>(v) >> (exp - SUB_BITS))
                               & (SUB - 1));
    return exp * SUB + sub;
}

std::int64_t LatencyRecorder::bucketMidpoint(int index) {
    if (index < SUB) {
        return index;
    }
    int exp = index / SUB;
    int sub = index % SUB;
    std::int64_t low = (static_cast<std::int64_t>(1) << exp)
            + (static_cast<std::int64_t>(sub) << (exp - SUB_BITS));
    std::int64_t width = static_cast<std::int64_t>(1) << (exp - SUB_BITS);
    return low + width / 2;
}

std::int64_t LatencyRecorder::percentile(double p) const {
    if (count_ == 0) {
        return 0;
    }
    if (!(p >= 0.0)) {   // also catches NaN, whose cast below would be undefined
        p = 0.0;
    }
    p = std::min(p, 1.0);
    std::int64_t target = static_cast<std::int64_t>(std::ceil(p * static_cast<double>(count_)));
    if (target <= 0) {
        target = 1;
    }
    std::int64_t cumulative = 0;
    for (int i = 0; i < 64 * SUB; i++) {
        cumulative += counts_[i];
        if (cumulative >= target) {
            std::int64_t est = bucketMidpoint(i);
            return std::max(min_, std::min(max_, est));
        }
    }
    return max_;
}

void LatencyRecorder::reset() {
    std::memset(counts_, 0, sizeof(counts_));
    count_ = 0;
    sum_ = 0;
    max_ = 0;
    min_ = std::numeric_limits<std::int64_t>::max();
}

namespace {
std::string fmt(std::int64_t nanos) {
    char buf[32];
    if (nanos < 1000) {
        std::snprintf(buf, sizeof(buf), "%lldns", static_cast<long long>(nanos));
    } else if (nanos < 1000000) {
        std::snprintf(buf, sizeof(buf), "%.1fus", static_cast<double>(nanos) / 1000.0);
    } else {
        std::snprintf(buf, sizeof(buf), "%.2fms", static_cast<double>(nanos) / 1000000.0);
    }
    return buf;
}
}  // namespace

std::string LatencyRecorder::summary() const {
    return "n=" + std::to_string(count_) + " mean=" + fmt(static_cast<std::int64_t>(mean()))
            + " p50=" + fmt(percentile(0.50)) + " p90=" + fmt(percentile(0.90))
            + " p99=" + fmt(percentile(0.99)) + " p99.9=" + fmt(percentile(0.999))
            + " max=" + fmt(max_);
}

}  // namespace quantfinlib
