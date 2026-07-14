#include "quantfinlib/pricing/fair_value_engine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace quantfinlib {

namespace {

/** Java Integer.highestOneBit for positive inputs. */
int highestOneBit(int x) {
    int bit = 1;
    while ((bit << 1) != 0 && (bit << 1) <= x) {
        bit <<= 1;
    }
    return x <= 0 ? 0 : bit;
}

}  // namespace

FairValueEngine::FairValueEngine(int capacity, std::int64_t windowNanos)
    : windowNanos_(windowNanos),
      microprice_(std::numeric_limits<double>::quiet_NaN()) {
    int cap = highestOneBit(std::max(2, capacity - 1)) * 2;
    mids_.assign(cap, 0.0);
    times_.assign(cap, 0);
    mask_ = cap - 1;
}

FairValueEngine::FairValueEngine() : FairValueEngine(256, 500'000'000LL) {}

void FairValueEngine::onQuote(double bid, double ask, double bidSize,
                              double askSize, std::int64_t timestampNanos) {
    microprice_ = microprice(bid, ask, bidSize, askSize);
    int i = static_cast<int>(head_ & mask_);
    mids_[i] = (bid + ask) / 2;
    times_[i] = timestampNanos;
    head_++;
}

double FairValueEngine::microprice(double bid, double ask, double bidSize,
                                   double askSize) {
    double total = bidSize + askSize;
    if (total <= 0 || std::isnan(bid) || std::isnan(ask)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double i = bidSize / total;
    return i * ask + (1 - i) * bid;
}

double FairValueEngine::microprice() const {
    return microprice_;
}

double FairValueEngine::driftPerSecond() const {
    if (head_ < 2) {
        return 0;
    }
    int newest = static_cast<int>((head_ - 1) & mask_);
    std::int64_t cutoff = times_[newest] - windowNanos_;
    std::int64_t oldestSeq = std::max<std::int64_t>(
            0, head_ - static_cast<std::int64_t>(mids_.size()));
    // Walk back to the oldest sample still inside the window.
    int oldest = newest;
    for (std::int64_t seq = head_ - 2; seq >= oldestSeq; seq--) {
        int i = static_cast<int>(seq & mask_);
        if (times_[i] < cutoff) {
            break;
        }
        oldest = i;
    }
    std::int64_t dt = times_[newest] - times_[oldest];
    if (dt <= 0) {
        return 0;
    }
    return (mids_[newest] - mids_[oldest]) / (static_cast<double>(dt) / 1e9);
}

double FairValueEngine::latencyAdjustedFair(std::int64_t latencyNanos) const {
    return microprice_
            + driftPerSecond() * static_cast<double>(latencyNanos) / 1e9;
}

}  // namespace quantfinlib
