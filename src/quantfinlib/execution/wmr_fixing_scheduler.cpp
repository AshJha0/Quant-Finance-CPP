#include "quantfinlib/execution/wmr_fixing_scheduler.hpp"

#include <limits>
#include <stdexcept>

#include "quantfinlib/execution/twap_scheduler.hpp"

namespace quantfinlib {
namespace wmr_fixing_scheduler {

std::vector<Slice> schedule(std::int64_t totalQty, std::int64_t windowMillis, int numSlices) {
    if (totalQty <= 0 || windowMillis <= 0 || numSlices < 1) {
        throw std::invalid_argument("need totalQty > 0, windowMillis > 0, numSlices >= 1");
    }
    if (numSlices > totalQty) {
        // Even slicing would emit zero-quantity children -- venue rejects
        // in the middle of the fixing window. Fail at schedule time.
        throw std::invalid_argument("numSlices (" + std::to_string(numSlices) + ") exceeds totalQty ("
                                     + std::to_string(totalQty) + "): zero-quantity child orders");
    }
    if (windowMillis > std::numeric_limits<std::int64_t>::max() / numSlices) {
        // Offset arithmetic (windowMillis * i) would overflow and place
        // slices BEFORE the window -- the pre-window execution this class
        // exists to refuse.
        throw std::invalid_argument("windowMillis too large: " + std::to_string(windowMillis));
    }
    // The schedule IS a TWAP over the window -- delegate so the claim
    // "TWAP-in-window is neutral replication" is true by construction and
    // cannot drift from the TWAP implementation.
    return twap_scheduler::schedule(totalQty, windowMillis, numSlices);
}

std::vector<Slice> schedule(std::int64_t totalQty, int numSlices) {
    return schedule(totalQty, WINDOW_MILLIS, numSlices);
}

}  // namespace wmr_fixing_scheduler
}  // namespace quantfinlib
