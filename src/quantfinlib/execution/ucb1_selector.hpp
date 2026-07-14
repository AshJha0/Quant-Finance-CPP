// Port of com.quantfinlib.execution.Ucb1Selector: UCB1 multi-armed bandit
// -- principled selection among venues, LPs or algo variants when the
// scorecards are still THIN. UCB1 (Auer et al. 2002) picks the arm
// maximizing mean reward + sqrt(2*ln(N)/n_i) -- the optimism bonus shrinks
// as an arm is tried, so exploration decays exactly as fast as the
// evidence accumulates, with logarithmic regret guaranteed. Rewards must
// be in [0, 1] (the theory's scale -- map fill quality, negated cost bps,
// or markout onto it).
//
// Deterministic (ties break to the lowest index), O(arms) per selection,
// allocation-free after construction. Research/warm lane.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class Ucb1Selector {
public:
    /** @param arms number of venues/variants, >= 2 */
    explicit Ucb1Selector(int arms);

    /**
     * The arm to use next: each arm once first (in index order), then
     * highest upper confidence bound, ties to the lowest index.
     */
    int select() const;

    /**
     * Records the observed reward for an arm.
     *
     * @param arm    the arm that was used
     * @param reward in [0, 1] -- the UCB1 theory's scale; rescale
     *               upstream, never here
     */
    void record(int arm, double reward);

    /** Times an arm has been used. */
    std::int64_t pulls(int arm) const { return pulls_[static_cast<std::size_t>(arm)]; }

    /** The arm's observed mean reward (NaN before its first pull). */
    double meanReward(int arm) const;

    std::int64_t totalPulls() const { return totalPulls_; }

private:
    std::vector<double> rewardSums_;
    std::vector<std::int64_t> pulls_;
    std::int64_t totalPulls_ = 0;
};

}  // namespace quantfinlib
