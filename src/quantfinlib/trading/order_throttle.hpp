// Port of com.quantfinlib.trading.OrderThrottle (Java): order-rate throttle
// -- a nanosecond token bucket for exchange message-rate limits (every real
// venue enforces one; exceeding it earns disconnects or fines, so the
// gateway must self-limit). Sustained rate ratePerSec with bursts up to
// burst -- a quiet spell banks up to one burst of headroom, then the
// bucket refills continuously.
//
// Single-writer (call from the order-entry thread), zero allocation, no
// clock reads of its own -- the caller passes the current time so tests
// are deterministic and the hot path controls its syscalls.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

class OrderThrottle {
public:
    /**
     * @param ratePerSec sustained messages per second (> 0)
     * @param burst      bucket depth: messages allowed back-to-back (>= 1)
     */
    OrderThrottle(double ratePerSec, int burst)
            : tokensPerNano_(ratePerSec / 1e9), burst_(burst), tokens_(burst) {
        if (ratePerSec <= 0 || burst < 1) {
            throw std::invalid_argument("need ratePerSec > 0, burst >= 1");
        }
    }

    /**
     * Attempts to take one send permit at nowNanos. False = do not send
     * (queue or drop per your policy -- this class only counts).
     */
    bool tryAcquire(std::int64_t nowNanos) {
        refill(nowNanos);
        // The 1e-9 slack absorbs floating-point drift from split refills
        // (a+b in two steps can sum a hair under one step): it grants at
        // most sub-nanosecond-early permits, never extra ones.
        if (tokens_ >= 1 - 1e-9) {
            tokens_ = std::max(0.0, tokens_ - 1);
            acquired_++;
            return true;
        }
        throttled_++;
        return false;
    }

    /**
     * Nanoseconds until a permit would be available (0 when one already
     * is) -- for pacing loops that would rather sleep than spin-fail.
     */
    std::int64_t nanosUntilAvailable(std::int64_t nowNanos) {
        refill(nowNanos);
        if (tokens_ >= 1) {
            return 0;
        }
        return static_cast<std::int64_t>(std::ceil((1 - tokens_) / tokensPerNano_));
    }

    /** Permits granted so far. */
    std::int64_t acquiredCount() const { return acquired_; }

    /** Denials so far -- a persistent nonzero rate means the strategy outruns the venue limit. */
    std::int64_t throttledCount() const { return throttled_; }

private:
    void refill(std::int64_t nowNanos) {
        if (lastNanos_ == std::numeric_limits<std::int64_t>::min()) {
            lastNanos_ = nowNanos;
            return;
        }
        std::int64_t dt = nowNanos - lastNanos_;
        if (dt > 0) {
            tokens_ = std::min(static_cast<double>(burst_), tokens_ + dt * tokensPerNano_);
            lastNanos_ = nowNanos;
        }
    }

    const double tokensPerNano_;
    const double burst_;
    double tokens_;
    std::int64_t lastNanos_ = std::numeric_limits<std::int64_t>::min();
    std::int64_t acquired_ = 0;
    std::int64_t throttled_ = 0;
};

}  // namespace quantfinlib
