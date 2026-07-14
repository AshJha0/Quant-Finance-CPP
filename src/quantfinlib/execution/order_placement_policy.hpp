// Port of com.quantfinlib.execution.OrderPlacementPolicy: the post-or-cross
// decision -- the smallest and most repeated choice in execution, made
// explicit as expected-cost arithmetic instead of habit. Relative to the
// current mid, for a buy:
//
//   cross now:   pay the half spread                          -> h
//   post at bid: filled (prob p):  earn h, pay adverse selection a,
//                collect the rebate r                          -> a - h - r
//                unfilled (1-p):   cross later after the market
//                drifted d against you                         -> h + d
//   expected post cost = p*(a - h - r) + (1-p)*(h + d)
//   POST iff that beats h
//
// The inputs are the honest parts: fillProbability from a fill-probability
// model, the adverse-selection cost `a` from post-fill markouts, and the
// drift `d` from your alpha (positive = expected to move against a
// waiting buyer). All in the same per-unit currency. Static, deterministic,
// research/warm lane. IllegalArgumentException maps to
// std::invalid_argument.
#pragma once

namespace quantfinlib {
namespace order_placement_policy {

/** The decision plus the arithmetic that made it. */
struct Placement {
    bool post;
    double expectedPostCost;
    double crossCost;
};

/**
 * @param halfSpread          half the touch spread, > 0
 * @param fillProbability     P(passive order fills within the horizon), in [0, 1]
 * @param adverseSelection    expected cost WHEN passively filled (post-fill markout), >= 0
 * @param adverseDrift        expected move against the order while waiting unfilled
 *                            (signed: negative = the market is expected to come to you)
 * @param rebate              maker rebate per unit, >= 0
 */
Placement decide(double halfSpread, double fillProbability, double adverseSelection, double adverseDrift,
                 double rebate);

/** The fill-probability REGION where posting beats crossing. */
struct PostRegion {
    double from;
    double to;

    bool isEmpty() const { return from > to; }
    bool contains(double fillProbability) const { return fillProbability >= from && fillProbability <= to; }
};

/**
 * The desk's rule for these market conditions: post iff the fill
 * probability lands inside the returned region of [0, 1]. A single
 * breakeven scalar cannot carry the answer: postCost(p) = (h+d) -
 * p*(2h+r+d-a) is linear in p, and when adverse selection exceeds 2h+r+d
 * the slope flips -- posting then pays only BELOW the threshold, not
 * above. Empty region = never post under these conditions. Boundaries are
 * indifference points (measure zero; decide crosses on a tie).
 */
PostRegion postRegion(double halfSpread, double adverseSelection, double adverseDrift, double rebate);

}  // namespace order_placement_policy
}  // namespace quantfinlib
