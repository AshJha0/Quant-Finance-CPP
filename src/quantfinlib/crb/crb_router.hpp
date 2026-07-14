// Port of com.quantfinlib.crb.CrbRouter (Java): the central risk book's
// order router — internal cross first, dark pools second, lit last.
// Internal crossing against the book's own offsetting inventory costs
// ZERO bps. Dark venues carry an adverse-selection charge in bps and are
// used only while that charge undercuts the lit cost; expected liquidity
// is discounted by fill probability. Whatever the dark legs are not
// expected to fill routes lit. Allocation greedy by expected cost,
// deterministic.
#pragma once

#include <string>
#include <vector>

namespace quantfinlib {
namespace crb_router {

/** A dark venue as the router sees it. */
struct DarkVenue {
    std::string name;
    double expectedLiquidity;
    double fillProbability;
    double adverseSelectionBps;

    DarkVenue(std::string name, double expectedLiquidity, double fillProbability,
             double adverseSelectionBps);
};

/**
 * Where the notional went. dark[i] aligns with the venue array passed in;
 * expectedCostBps is the blended expected cost of the whole allocation.
 */
struct Allocation {
    double internalNotional;
    std::vector<double> dark;
    double lit;
    double expectedCostBps;
};

/**
 * Routes notional (positive, in book-currency units).
 *
 * @param notional          amount to execute, > 0
 * @param crossableInternal offsetting book inventory available to cross
 *                          against, >= 0
 * @param venues            dark venues with honest statistics
 * @param halfSpreadBps     lit half spread, > 0
 * @param impactBps         expected lit impact for this size, >= 0
 */
Allocation route(double notional, double crossableInternal, const std::vector<DarkVenue>& venues,
                 double halfSpreadBps, double impactBps);

}  // namespace crb_router
}  // namespace quantfinlib
