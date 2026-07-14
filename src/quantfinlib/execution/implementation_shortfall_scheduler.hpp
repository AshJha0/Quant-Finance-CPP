// Port of com.quantfinlib.execution.ImplementationShortfallScheduler:
// implementation-shortfall (arrival-price) schedule -- turns the
// AlmgrenChriss optimal trajectory into executable Slices. Where TWAP
// spreads evenly and VWAP follows the volume curve, IS front-loads with
// urgency kappa -- trade more now to cut exposure to price drift, but not
// so fast that temporary impact dominates; risk aversion lambda sets the
// balance (lambda -> 0 degrades to TWAP, exactly as the math says).
//
// Slice quantities are integer-allocated largest-remainder style so they
// always sum exactly to the parent quantity. IllegalArgumentException
// maps to std::invalid_argument.
#pragma once

#include <cstdint>
#include <vector>

#include "quantfinlib/execution/slice.hpp"
#include "quantfinlib/microstructure/almgren_chriss.hpp"

namespace quantfinlib {
namespace implementation_shortfall_scheduler {

/**
 * The optimal IS schedule for the given market parameters.
 *
 * @param params         Almgren-Chriss inputs (totalShares is the parent)
 * @param durationMillis wall-clock execution window the horizon maps onto
 * @throws std::invalid_argument if the AC trajectory overflows (sinh
 *         overflow around kappa*T ~ 710) or durationMillis is out of range
 */
std::vector<Slice> schedule(const AlmgrenChriss::Params& params, std::int64_t durationMillis);

/**
 * Convenience urgency calibration: the risk aversion whose first slice is
 * roughly `frontLoadFraction` of the parent (e.g. 0.3 = "30% up front"),
 * found by bisection on lambda. Useful when traders think in front-load,
 * not in lambda.
 *
 * @throws std::invalid_argument if frontLoadFraction is out of range, or
 *         unreachable for these parameters
 */
double riskAversionForFrontLoad(const AlmgrenChriss::Params& base, double frontLoadFraction);

}  // namespace implementation_shortfall_scheduler
}  // namespace quantfinlib
