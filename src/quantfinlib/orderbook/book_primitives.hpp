// Port of com.quantfinlib.orderbook.BookPrimitives (Java): the
// zero-allocation building blocks shared by the hot-lane books --
// occupancy-bitmap scans and a primitive open-addressing long->int map
// with backward-shift deletion. Free inline functions over caller-owned
// arrays, so sharing costs nothing on the hot path and the subtlest code
// in the library (the circular probe-order rule in mapRemoveAt) exists
// exactly once.
//
// Map convention: key 0 is the empty-slot sentinel (both books use
// strictly positive ids/refs); capacity is a power of two with
// mask = capacity - 1 and load factor <= 0.5. Scans return -1 for "none".
#pragma once

#include <bit>
#include <cstdint>

namespace quantfinlib {
namespace book_primitives {

inline constexpr int kNone = -1;

/** Stafford variant 13 finalizer: cheap, well-mixed 64-bit hash. */
inline std::uint64_t mix(std::uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

/** Lowest set bit at or above `from`, or -1. Bits past the ladder must be clear. */
inline int nextSetAtOrAbove(const std::uint64_t* bits, int words, int from) {
    if (from < 0) {
        from = 0;
    }
    int word = from >> 6;
    if (word >= words) {
        return kNone;
    }
    std::uint64_t w = bits[word] & (~0ULL << (from & 63));  // mask below `from`
    while (true) {
        if (w != 0) {
            return (word << 6) + std::countr_zero(w);
        }
        if (++word >= words) {
            return kNone;
        }
        w = bits[word];
    }
}

/** Highest set bit at or below `from`, or -1. */
inline int nextSetAtOrBelow(const std::uint64_t* bits, int words, int from) {
    if (from < 0) {
        return kNone;
    }
    int max = (words << 6) - 1;
    if (from > max) {
        from = max;
    }
    int word = from >> 6;
    std::uint64_t w = bits[word] & (~0ULL >> (63 - (from & 63)));  // mask above `from`
    while (true) {
        if (w != 0) {
            return (word << 6) + 63 - std::countl_zero(w);
        }
        if (--word < 0) {
            return kNone;
        }
        w = bits[word];
    }
}

/** Inserts into the open-addressing map (caller guarantees the key is absent). */
inline void mapPut(std::int64_t* keys, int* vals, int mask, std::int64_t key, int value) {
    int slot = static_cast<int>(mix(static_cast<std::uint64_t>(key))) & mask;
    while (keys[slot] != 0) {
        slot = (slot + 1) & mask;
    }
    keys[slot] = key;
    vals[slot] = value;
}

/** Slot of `key`, or -1. */
inline int mapFind(const std::int64_t* keys, int mask, std::int64_t key) {
    int slot = static_cast<int>(mix(static_cast<std::uint64_t>(key))) & mask;
    while (keys[slot] != 0) {
        if (keys[slot] == key) {
            return slot;
        }
        slot = (slot + 1) & mask;
    }
    return kNone;
}

/**
 * Backward-shift deletion: re-places every entry of the probe run that
 * follows the hole, so lookups never need tombstones and cancel churn
 * cannot degrade probe lengths over a long session.
 */
inline void mapRemoveAt(std::int64_t* keys, int* vals, int mask, int slot) {
    int hole = slot;
    int probe = (hole + 1) & mask;
    while (keys[probe] != 0) {
        int home = static_cast<int>(mix(static_cast<std::uint64_t>(keys[probe]))) & mask;
        // Move back iff the entry's home position is "at or before" the
        // hole in circular probe order (standard backward-shift rule).
        bool move = hole <= probe
                            ? (home <= hole || home > probe)
                            : (home <= hole && home > probe);
        if (move) {
            keys[hole] = keys[probe];
            vals[hole] = vals[probe];
            hole = probe;
        }
        probe = (probe + 1) & mask;
    }
    keys[hole] = 0;
}

}  // namespace book_primitives
}  // namespace quantfinlib
