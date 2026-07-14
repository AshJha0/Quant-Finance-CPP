// Port of com.quantfinlib.fx.LpScorecard (Java): streaming per-LP execution
// quality — the taker-side answer to last look. Every statistic is
// exponentially weighted per event (configurable alpha), so the card
// tracks CURRENT LP behavior. Post-reject markout is measured one horizon
// after each reject against the mid fed via onMid: positive markout means
// the market moved the way you were trying to trade — the reject cost you
// real money. Zero allocation, single writer. Pending markouts live in a
// small ring per LP (PENDING_RING slots): reject bursts are sampled rather
// than overwritten.
//
// DEVIATION FROM JAVA: writeState/readState (persist.Checkpoint section
// serialization) are NOT ported — persist.Checkpoint has no C++ port in
// this codebase and is out of this task's scope (fx/crb classes only).
// Everything else — the full streaming statistics surface — is ported 1:1.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class LpScorecard {
public:
    /** Pending-markout slots per LP (bursts deeper than this overwrite oldest). */
    static constexpr int PENDING_RING = 4;

    /**
     * @param lpCount             number of LPs (dense indices)
     * @param alpha               EWMA weight per event, e.g. 0.05
     * @param markoutHorizonNanos how long after a reject the markout is
     *                            read, e.g. 100ms = 100'000'000
     */
    LpScorecard(int lpCount, double alpha, std::int64_t markoutHorizonNanos);

    /** 5% event weight, 100 ms markout horizon. */
    explicit LpScorecard(int lpCount);

    /**
     * An accepted fill.
     *
     * @param buy          our direction
     * @param price        the all-in fill price
     * @param midAtRequest composite mid when the order was sent
     * @param holdNanos    time the LP held the order before accepting
     */
    void onFill(int lp, bool buy, double price, double midAtRequest, std::int64_t holdNanos);

    /**
     * A last-look reject. The markout clock starts here: feed mids via
     * onMid and the move one horizon later is attributed to this reject.
     */
    void onReject(int lp, bool buy, double midAtRequest, std::int64_t timestampNanos,
                 std::int64_t holdNanos);

    /**
     * Composite mid update: matures every pending reject markout whose
     * horizon has elapsed. NaN mids are ignored.
     */
    void onMid(double mid, std::int64_t timestampNanos);

    /** EWMA reject probability in [0, 1]; 0 before any events. */
    double rejectRate(int lp) const { return rejectRate_[lp]; }

    /** EWMA hold time across fills and rejects, in nanos. */
    double avgHoldNanos(int lp) const { return holdNanosEwma_[lp]; }

    /** EWMA effective half-spread paid on fills, in price units. */
    double effectiveSpread(int lp) const { return effSpreadEwma_[lp]; }

    /**
     * EWMA post-reject markout in price units — positive means the market
     * moved the way you were trying to trade after the LP declined.
     */
    double postRejectMarkout(int lp) const { return markoutEwma_[lp]; }

    /** Markouts matured across all LPs — the router-degradation canary. */
    std::int64_t maturedMarkouts() const { return maturedMarkouts_; }

    /** Invariant: attempts == fills + rejects (kept explicit for future outcomes). */
    std::int64_t attempts(int lp) const { return attempts_[lp]; }
    std::int64_t fills(int lp) const { return fills_[lp]; }
    std::int64_t rejects(int lp) const { return rejects_[lp]; }
    int lpCount() const { return lpCount_; }

private:
    int lpCount_;
    double alpha_;
    std::int64_t markoutHorizonNanos_;

    std::vector<std::int64_t> attempts_;
    std::vector<std::int64_t> fills_;
    std::vector<std::int64_t> rejects_;
    std::vector<double> rejectRate_;
    std::vector<double> holdNanosEwma_;
    std::vector<double> effSpreadEwma_;
    std::vector<double> markoutEwma_;
    std::vector<std::int64_t> markoutCount_;

    // Pending markout ring [lp * PENDING_RING + k]: side 0 = empty, +1 buy, -1 sell.
    std::vector<std::int8_t> pendingSide_;
    std::vector<double> pendingMid_;
    std::vector<std::int64_t> pendingTs_;
    std::vector<std::uint8_t> pendingCursor_;
    int pendingCount_;
    std::int64_t maturedMarkouts_;
};

}  // namespace quantfinlib
