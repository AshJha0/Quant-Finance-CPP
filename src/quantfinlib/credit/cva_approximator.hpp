// Port of com.quantfinlib.credit.CvaApproximator (Java): UNILATERAL CVA,
// the standard discrete approximation
//   CVA = LGD * sum_i EE(t_i) * [ Q(t_{i-1}) - Q(t_i) ] * DF(t_i)
// — expected exposure per bucket, times the probability of defaulting IN
// that bucket (off the CreditCurve's survival function), times the
// discount factor, times loss-given-default.
//
// Approximations, stated (copied from the Java doc): exposure is
// evaluated at the bucket END and assumed constant across the bucket
// (O(dt) bias); default and exposure are INDEPENDENT — no wrong-way
// risk; unilateral — your own default (DVA) is not netted; LGD is a
// constant passed by the caller, usually 1 - recovery, but kept separate
// because the curve's recovery is a quoting convention while CVA's LGD
// is a modeling choice.
#pragma once

#include <vector>

#include "quantfinlib/credit/credit_curve.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace cva_approximator {

/**
 * Discrete unilateral CVA over the given exposure profile.
 * expectedExposure: EE(t_i) per bucket, >= 0; bucketEndYears: bucket end
 * times t_i in years, strictly ascending, all > 0 (t_0 = 0 is implicit);
 * lgd: loss given default in (0, 1].
 * Returns the CVA charge (positive; subtract it from the risk-free PV).
 */
double cva(const std::vector<double>& expectedExposure,
           const std::vector<double>& bucketEndYears,
           const CreditCurve& counterparty, const YieldCurve& discount, double lgd);

}  // namespace cva_approximator
}  // namespace quantfinlib
