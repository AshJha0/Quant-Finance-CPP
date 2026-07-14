// Port of com.quantfinlib.fx.LpRouter (Java): last-look-aware LP router —
// chooses where to send an FX clip by EXPECTED all-in price, not displayed
// price. Expected cost per LP: quoted price +/- rejectRate * max(postRejectMarkout, 0)
// (worse for the taker on either side), with LPs above a configurable
// reject-rate cap vetoed outright. Full-amount only, one LP per clip. Zero
// allocation, single writer.
#pragma once

#include <cstdint>

#include "quantfinlib/fx/fx_tier_book.hpp"
#include "quantfinlib/fx/lp_scorecard.hpp"

namespace quantfinlib {

class LpRouter {
public:
    /**
     * @param maxRejectRate LPs whose EWMA reject rate exceeds this are
     *                      vetoed regardless of price (e.g. 0.25)
     */
    LpRouter(const FxTierBook& book, const LpScorecard& card, double maxRejectRate);

    /**
     * With a hold-time urgency: a positive holdUrgencyBpsPerMs charges each
     * LP's EWMA hold time against its quote (bps of price per millisecond
     * held).
     */
    LpRouter(const FxTierBook& book, const LpScorecard& card, double maxRejectRate,
            double holdUrgencyBpsPerMs);

    /**
     * Chooses the LP for a full-amount clip. Returns the LP index, or -1
     * when no eligible LP quotes the size.
     */
    int route(bool buy, double size);

    /** Raw quoted price behind the last successful route(); NaN otherwise. */
    double lastQuotedPrice() const { return lastQuotedPrice_; }

    /** Reject-adjusted price behind the last successful route(); NaN otherwise. */
    double lastExpectedPrice() const { return lastExpectedPrice_; }

    std::int64_t routeCount() const { return routeCount_; }

    /** LP-candidate evaluations skipped for exceeding the reject-rate cap. */
    std::int64_t vetoCount() const { return vetoCount_; }

private:
    const FxTierBook* book_;
    const LpScorecard* card_;
    double maxRejectRate_;
    double holdUrgencyBpsPerMs_;

    double lastQuotedPrice_;
    double lastExpectedPrice_;
    std::int64_t routeCount_;
    std::int64_t vetoCount_;
};

}  // namespace quantfinlib
