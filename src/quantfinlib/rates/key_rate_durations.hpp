// Port of com.quantfinlib.rates.KeyRateDurations (Java): WHERE on the
// curve a bond's rate risk lives. For each curve tenor, the zero rate at
// that node is bumped +/-1bp (all other nodes fixed — the curve's own
// interpolation spreads the bump between neighbors, which IS the
// standard convention), the bond is repriced off each bumped curve, and
// the central difference gives the sensitivity. The vector of per-node
// sensitivities sums back to the parallel duration — a consistency check
// the tests pin. Prices per 100 face follow the BondPricer convention.
#pragma once

#include <vector>

#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace key_rate_durations {

/**
 * Per-node price sensitivities of a fixed-coupon bond to a 1bp bump of
 * each curve tenor, in price units per 100 face (positive = the bond
 * LOSES that much when the node rises 1bp — DV01 sign convention).
 * Index i corresponds to curve.tenors()[i] ascending.
 */
std::vector<double> keyRateDv01s(double face, double couponRate, int frequency,
                                 double maturityYears, const YieldCurve& curve);

/**
 * The parallel DV01 off the curve (every node bumped together) — the
 * number the key-rate slices must add back up to, within the
 * curve-interpolation tolerance the tests document.
 */
double parallelDv01(double face, double couponRate, int frequency,
                    double maturityYears, const YieldCurve& curve);

}  // namespace key_rate_durations
}  // namespace quantfinlib
