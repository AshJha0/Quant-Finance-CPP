// Bit-exact C++ implementation of java.util.SplittableRandom's
// single-stream path (the golden-gamma constructor): mix64 is Stafford
// variant 13, mix32 the murmur3-style high-half mix, nextInt(bound) the
// same rejection loop (Java's int overflow reproduced with unsigned
// arithmetic, which wraps identically). Everything the Java library
// seeds deterministically (Monte Carlo shuffles, optimizer sampling,
// bootstrap paths) therefore draws the SAME stream here, so seeded
// results agree across the two ports.
#pragma once

#include <cstdint>
#include <stdexcept>

namespace quantfinlib {

class SplittableRandom {
public:
    explicit SplittableRandom(std::int64_t seed)
            : seed_(static_cast<std::uint64_t>(seed)) {}

    std::int64_t nextLong() { return static_cast<std::int64_t>(mix64(nextSeed())); }

    /** Uniform in [0, 1): top 53 bits of mix64, exactly Java's DOUBLE_UNIT scaling. */
    double nextDouble() {
        return static_cast<double>(mix64(nextSeed()) >> 11) * 0x1.0p-53;
    }

    /** Uniform in [0, bound); bound > 0. Java's power-of-two fast path + rejection. */
    int nextInt(int bound) {
        if (bound <= 0) {
            throw std::invalid_argument("bound must be positive");
        }
        std::uint32_t r = mix32(nextSeed());
        std::uint32_t m = static_cast<std::uint32_t>(bound) - 1;
        if ((static_cast<std::uint32_t>(bound) & m) == 0) {  // power of two
            r &= m;
        } else {
            // Java: for (int u = r >>> 1; u + m - (r = u % bound) < 0; u = next >>> 1);
            // signed-overflow test done in uint32 (wraps like Java int).
            for (std::uint32_t u = r >> 1;
                 static_cast<std::int32_t>(u + m - (r = u % static_cast<std::uint32_t>(bound))) < 0;
                 u = mix32(nextSeed()) >> 1) {
            }
        }
        return static_cast<int>(r);
    }

private:
    static constexpr std::uint64_t GOLDEN_GAMMA = 0x9E3779B97F4A7C15ULL;

    std::uint64_t nextSeed() { return seed_ += GOLDEN_GAMMA; }

    static std::uint64_t mix64(std::uint64_t z) {
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }

    static std::uint32_t mix32(std::uint64_t z) {
        z = (z ^ (z >> 33)) * 0xFF51AFD7ED558CCDULL;
        return static_cast<std::uint32_t>(((z ^ (z >> 33)) * 0xC4CEB9FE1A85EC53ULL) >> 32);
    }

    std::uint64_t seed_;
};

}  // namespace quantfinlib
