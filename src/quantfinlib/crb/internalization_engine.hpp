// Port of com.quantfinlib.crb.InternalizationEngine (Java): the
// internalize-or-route decision. Risk-REDUCING flow (opposite sign to the
// book's net) is internalized up to the offsetting inventory, with a share
// of the saved spread given back as price improvement. Risk-ADDING flow is
// warehoused (no improvement) only while post-trade inventory stays inside
// the warehouse limit; beyond that it routes out. Deterministic,
// single-threaded.
//
// DEVIATION FROM JAVA: writeState/readState (persist.Checkpoint section
// serialization) are NOT ported — see lp_scorecard.hpp for the rationale
// (no C++ persist.Checkpoint port; out of this task's scope).
#pragma once

namespace quantfinlib {

class InternalizationEngine {
public:
    /** Where one flow went, and what the client got for it. */
    struct Decision {
        double internalized;
        double routed;
        double improvementBps;
    };

    /**
     * @param warehouseLimit   max |inventory| the book will hold on a
     *                         factor after absorbing risk-adding flow, > 0
     * @param improvementShare fraction of the half spread returned to a
     *                         risk-reducing client, in [0, 1]
     */
    InternalizationEngine(double warehouseLimit, double improvementShare);

    /**
     * Decides one flow against the book's current net on that factor.
     *
     * @param bookNet       the book's net exposure on the factor (signed)
     * @param flow          exposure change the book absorbs if it
     *                      internalizes (signed, non-zero)
     * @param halfSpreadBps the street's half spread for this risk, > 0
     */
    Decision decide(double bookNet, double flow, double halfSpreadBps);

    /** Fraction of decided notional the book kept (0 before any flow). */
    double internalizationRate() const;

    double internalizedNotional() const { return internalizedNotional_; }
    double routedNotional() const { return routedNotional_; }

private:
    double warehouseLimit_;
    double improvementShare_;
    double internalizedNotional_ = 0;
    double routedNotional_ = 0;
};

}  // namespace quantfinlib
