// Port of com.quantfinlib.crb.CrbPnlLedger (Java): the central risk book's
// ECONOMICS ledger. Internalized client flow captures the street half
// spread minus whatever improvement was given back
// (notional*(halfSpread-improvement)/1e4); routed flow captures nothing;
// hedge executions cost their all-in bps; router allocations cost their
// blended expected bps. Inventory mark-to-market P&L is deliberately out
// of scope. All notionals positive, all bps non-negative, book-currency
// units. Deterministic, single-threaded.
//
// DEVIATION FROM JAVA: writeState/readState (persist.Checkpoint section
// serialization) are NOT ported — see lp_scorecard.hpp for the rationale.
#pragma once

#include "quantfinlib/crb/internalization_engine.hpp"
#include "quantfinlib/crb/crb_router.hpp"

namespace quantfinlib {

class CrbPnlLedger {
public:
    /**
     * Records one internalization decision's economics.
     *
     * @param internalizedNotional |notional| kept on the book, >= 0
     * @param halfSpreadBps        the street half spread saved, > 0
     * @param improvementBps       improvement given to the client, >= 0
     *                             and <= halfSpreadBps
     */
    void onInternalized(double internalizedNotional, double halfSpreadBps,
                       double improvementBps);

    /** Convenience: books a whole InternalizationEngine::Decision. */
    void onDecision(const InternalizationEngine::Decision& decision, double halfSpreadBps);

    /** Records a hedge execution's all-in cost. */
    void onHedge(double notional, double costBps);

    /** Records a router allocation's blended expected cost. */
    void onRoute(double notional, const crb_router::Allocation& allocation);

    /** Spread captured by internalizing, net of improvement given back. */
    double spreadCaptured() const { return spreadCaptured_; }

    /** Improvement handed to clients — the cost of being worth trading with. */
    double improvementPaid() const { return improvementPaid_; }

    double hedgeCost() const { return hedgeCost_; }
    double routerCost() const { return routerCost_; }

    /**
     * The desk's realized economics: captured spread minus hedging and
     * routing costs.
     */
    double netEconomics() const { return spreadCaptured_ - hedgeCost_ - routerCost_; }

    long internalizations() const { return internalizations_; }
    long hedges() const { return hedges_; }

private:
    double spreadCaptured_ = 0;
    double improvementPaid_ = 0;
    double hedgeCost_ = 0;
    double routerCost_ = 0;
    long internalizations_ = 0;
    long hedges_ = 0;
};

}  // namespace quantfinlib
