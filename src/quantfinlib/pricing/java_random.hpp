// Bit-exact ports of java.util.Random (48-bit LCG, Marsaglia polar
// nextGaussian with its one-value cache) and java.util.SplittableRandom
// (Stafford variant-13 mix64 over a golden-gamma Weyl sequence). The Java
// pricing tests pin seeded Monte Carlo values and a seeded SABR
// calibration; reproducing the exact draw streams lets every such pin
// transfer unchanged. The only possible divergence from the JVM is
// StrictMath.log vs std::log inside nextGaussian (<= 1 ulp per draw),
// far below every tolerance used.
#pragma once

#include <cmath>
#include <cstdint>

namespace quantfinlib {

/** java.util.Random: seed scramble, next(bits), nextDouble, nextGaussian. */
class JavaRandom {
public:
    explicit JavaRandom(std::int64_t seed)
        : seed_((static_cast<std::uint64_t>(seed) ^ MULTIPLIER) & MASK) {}

    double nextDouble() {
        // Java: (((long) next(26) << 27) + next(27)) * 0x1.0p-53 — the two
        // next() calls MUST be sequenced left to right.
        std::int64_t hi = static_cast<std::int64_t>(next(26)) << 27;
        std::int64_t lo = next(27);
        return static_cast<double>(hi + lo) * 0x1.0p-53;
    }

    /** Marsaglia polar method with Java's cached second deviate. */
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

    std::int32_t next(int bits) {
        seed_ = (seed_ * MULTIPLIER + ADDEND) & MASK;
        return static_cast<std::int32_t>(seed_ >> (48 - bits));
    }

    std::uint64_t seed_;
    bool haveNextNextGaussian_ = false;
    double nextNextGaussian_ = 0;
};

/** java.util.SplittableRandom(seed) with the default golden gamma. */
class JavaSplittableRandom {
public:
    explicit JavaSplittableRandom(std::int64_t seed)
        : seed_(static_cast<std::uint64_t>(seed)) {}

    std::uint64_t nextLong() { return mix64(seed_ += GOLDEN_GAMMA); }

    double nextDouble() {
        return static_cast<double>(nextLong() >> 11) * 0x1.0p-53;
    }

private:
    static constexpr std::uint64_t GOLDEN_GAMMA = 0x9e3779b97f4a7c15ULL;

    static std::uint64_t mix64(std::uint64_t z) {
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    }

    std::uint64_t seed_;
};

}  // namespace quantfinlib
