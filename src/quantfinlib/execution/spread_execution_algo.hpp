// Port of com.quantfinlib.execution.SpreadExecutionAlgo: two-legged spread
// execution with LEGGING-RISK control -- pairs trades, cash-vs-futures
// basis, stub-vs-hedge: the trade is the SPREAD, and the risk is the
// moment you own one leg without the other. The discipline every spread
// desk runs: the LEAD leg (illiquid, the constraint) is worked patiently;
// the HEDGE leg (liquid) CHASES the lead leg's fills at the spread ratio;
// the legging imbalance |executedLead*ratio - executedHedge| is capped --
// at the cap the algo stops adding lead risk entirely and the hedge child
// becomes the full imbalance.
//
// Quantities are positive per leg; the ratio is hedge units per lead unit.
// Deterministic, single-threaded, research/warm lane.
// IllegalArgumentException maps to std::invalid_argument.
#pragma once

#include <cstdint>

namespace quantfinlib {

class SpreadExecutionAlgo {
public:
    /** This interval's child quantities, per leg. */
    struct Children {
        std::int64_t leadQty;
        std::int64_t hedgeQty;
        bool atRiskCap;
    };

    /**
     * @param leadParentQty total lead-leg quantity to execute, > 0
     * @param hedgePerLeadUnit spread ratio: hedge units per lead unit, > 0
     * @param leggingLimitHedgeUnits max |lead*ratio - hedge| imbalance
     *                               tolerated, >= 1 hedge unit
     * @param leadChildMax largest lead child per decision, > 0 (the
     *                     patience knob -- small children, passive fills)
     */
    SpreadExecutionAlgo(std::int64_t leadParentQty, double hedgePerLeadUnit,
                        std::int64_t leggingLimitHedgeUnits, std::int64_t leadChildMax);

    /**
     * The next children. The hedge chases the CURRENT imbalance; the lead
     * child is sized so even a full fill cannot push the projected
     * imbalance past the cap.
     */
    Children decide() const;

    /** Lead-leg fill. */
    void onLeadFill(std::int64_t qty);

    /**
     * Hedge-leg fill. Bounded by the full spread's hedge target -- a fill
     * beyond it is a duplicate report, the same upstream bug the lead-leg
     * guard exists to catch.
     */
    void onHedgeFill(std::int64_t qty);

    /** Current legging imbalance in hedge units (positive = hedge behind). */
    std::int64_t imbalanceHedgeUnits() const;

    std::int64_t leadExecuted() const { return leadExecuted_; }
    std::int64_t hedgeExecuted() const { return hedgeExecuted_; }

    /** Done when the lead is complete AND the hedge has caught up. */
    bool done() const;

private:
    std::int64_t leadParent_;
    double ratio_;
    std::int64_t leggingLimit_;
    std::int64_t leadChildMax_;
    std::int64_t leadExecuted_ = 0;
    std::int64_t hedgeExecuted_ = 0;
};

}  // namespace quantfinlib
