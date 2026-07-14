// Port of com.quantfinlib.util.LatencyRecorder (Java): zero-allocation
// nanosecond latency histogram (HdrHistogram-style log-linear buckets:
// 16 sub-buckets per power of two, ~6% worst-case quantile error).
// record() is a couple of array writes -- safe to call on the hot path.
// Single-writer; not thread-safe.
#pragma once

#include <cstdint>
#include <string>

namespace quantfinlib {

class LatencyRecorder {
public:
    static constexpr int SUB_BITS = 4;
    static constexpr int SUB = 1 << SUB_BITS;

    LatencyRecorder() { reset(); }

    void record(std::int64_t nanos) {
        if (nanos < 0) {
            nanos = 0;
        }
        count_++;
        sum_ += nanos;
        if (nanos > max_) {
            max_ = nanos;
        }
        if (nanos < min_) {
            min_ = nanos;
        }
        counts_[bucketIndex(nanos)]++;
    }

    static int bucketIndex(std::int64_t v);

    /** Estimated latency at quantile `p` in [0, 1], in nanoseconds. */
    std::int64_t percentile(double p) const;

    std::int64_t count() const { return count_; }
    std::int64_t max() const { return max_; }
    std::int64_t min() const { return count_ == 0 ? 0 : min_; }
    double mean() const {
        return count_ == 0 ? 0 : static_cast<double>(sum_) / static_cast<double>(count_);
    }

    void reset();

    /** One-line summary: p50/p90/p99/p99.9/max in appropriate units. */
    std::string summary() const;

private:
    static std::int64_t bucketMidpoint(int index);

    std::int64_t counts_[64 * SUB];
    std::int64_t count_ = 0;
    std::int64_t sum_ = 0;
    std::int64_t max_ = 0;
    std::int64_t min_ = 0;
};

}  // namespace quantfinlib
