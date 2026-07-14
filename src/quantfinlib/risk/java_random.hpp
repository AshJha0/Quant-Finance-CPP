// Exact re-implementation of java.util.Random: the JDK's 48-bit linear
// congruential generator, seed' = (seed * 0x5DEECE66D + 0xB) mod 2^48,
// with the initial seed scramble (seed ^ 0x5DEECE66D) and the Marsaglia
// polar nextGaussian() carrying its one-value cache. Ported so the C++
// risk tests replay the EXACT random streams the Java tests pin
// (VarEngine Monte Carlo, copula sampling, EVT's planted Pareto tail);
// with the same seed every nextDouble()/nextGaussian() matches Java
// bit-for-bit up to libm ulp differences in log/sqrt.
#pragma once

#include <cmath>
#include <cstdint>

namespace quantfinlib {

class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed) { setSeed(seed); }

    void setSeed(std::int64_t seed) {
        seed_ = (static_cast<std::uint64_t>(seed) ^ MULTIPLIER) & MASK;
        haveNextNextGaussian_ = false;
    }

    /** Uniform in [0, 1), 53 random bits — java.util.Random#nextDouble. */
    double nextDouble() {
        std::int64_t hi = static_cast<std::int64_t>(next(26)) << 27;
        return static_cast<double>(hi + next(27)) * DOUBLE_UNIT;
    }

    /** Standard normal via Marsaglia polar — java.util.Random#nextGaussian. */
    double nextGaussian() {
        if (haveNextNextGaussian_) {
            haveNextNextGaussian_ = false;
            return nextNextGaussian_;
        }
        double v1;
        double v2;
        double s;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            s = v1 * v1 + v2 * v2;
        } while (s >= 1 || s == 0);
        double multiplier = std::sqrt(-2 * std::log(s) / s);
        nextNextGaussian_ = v2 * multiplier;
        haveNextNextGaussian_ = true;
        return v1 * multiplier;
    }

private:
    static constexpr std::uint64_t MULTIPLIER = 0x5DEECE66DULL;
    static constexpr std::uint64_t ADDEND = 0xBULL;
    static constexpr std::uint64_t MASK = (1ULL << 48) - 1;
    static constexpr double DOUBLE_UNIT = 1.0 / 9007199254740992.0;  // 0x1.0p-53

    std::uint64_t seed_;
    bool haveNextNextGaussian_ = false;
    double nextNextGaussian_ = 0;

    /** Top `bits` bits of the advanced 48-bit state, as Java's next(int). */
    std::int32_t next(int bits) {
        seed_ = (seed_ * MULTIPLIER + ADDEND) & MASK;
        return static_cast<std::int32_t>(seed_ >> (48 - bits));
    }
};

}  // namespace quantfinlib
