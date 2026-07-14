// Port of com.quantfinlib.crb.SkewedQuoter (Java): inventory-skewed
// two-way pricing. A CRB long inventory shades BOTH quotes down: the ask
// gets more attractive (sell what we hold), the bid less (stop
// accumulating). Skew is linear in inventory as a fraction of the
// inventory limit, capped so the quote NEVER crosses itself:
//   skew = -(inventory/limit)*skewFraction*halfSpread   (clamped +-1)
//   bid  = mid*(1 + (-halfSpread + skew)/1e4)
//   ask  = mid*(1 + (+halfSpread + skew)/1e4)
#pragma once

namespace quantfinlib {
namespace skewed_quoter {

/** A shaded two-way price. */
struct Quote {
    double bid;
    double ask;
    double skewBps;
};

/**
 * @param mid            current fair value, > 0
 * @param halfSpreadBps  unskewed half spread in bps, > 0
 * @param inventory      book inventory in the quoted factor's units (signed)
 * @param inventoryLimit inventory limit, > 0 (same units)
 * @param skewFraction   how much of the half spread a full-limit inventory
 *                       shades, in [0, 1)
 */
Quote quote(double mid, double halfSpreadBps, double inventory, double inventoryLimit,
           double skewFraction);

}  // namespace skewed_quoter
}  // namespace quantfinlib
