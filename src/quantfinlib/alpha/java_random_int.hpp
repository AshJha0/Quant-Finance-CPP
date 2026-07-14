// Minimal java.util.Random port supplying nextInt(bound) for the
// Fisher-Yates permutation test in AlphaValidation::monteCarloRobustness.
// Kept local to alpha/ (the shared risk/pricing JavaRandom ports don't
// expose nextInt) but the same 48-bit LCG, so a seeded permutation
// reproduces the Java source's shuffle bit-for-bit.
#pragma once

#include <cstdint>
#include <stdexcept>

namespace quantfinlib {
namespace alpha_detail {

class JavaRandomInt {
public:
    explicit JavaRandomInt(std::int64_t seed)
            : seed_((static_cast<std::uint64_t>(seed) ^ MULTIPLIER) & MASK) {}

    /** java.util.Random#nextInt(int): uniform in [0, bound). */
    int nextInt(int bound) {
        if (bound <= 0) {
            throw std::invalid_argument("bound must be positive");
        }
        if ((bound & -bound) == bound) {  // power of two
            return static_cast<int>((static_cast<std::int64_t>(bound) * static_cast<std::int64_t>(next(31))) >> 31);
        }
        int bits;
        int val;
        do {
            bits = next(31);
            val = bits % bound;
        } while (bits - val + (bound - 1) < 0);
        return val;
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
};

}  // namespace alpha_detail
}  // namespace quantfinlib
