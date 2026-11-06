// Port of com.quantfinlib.rates.MultiCurveBootstrap (Java): post-2008 curve
// construction. bootstrap() solves a curve that both discounts and projects
// (OIS). bootstrapProjection() solves a term-rate curve against a fixed
// discount curve. Stage one walks the pillars with a secant-Newton step per
// instrument; stage two is a global Newton on all quotes, which monotone
// convex needs because a later pillar moves earlier forwards. Every input
// reprices within TOLERANCE, or the solve throws std::runtime_error.
#pragma once

#include <vector>

#include "quantfinlib/rates/curve_instruments.hpp"

namespace quantfinlib {
namespace multi_curve_bootstrap {

inline constexpr double TOLERANCE = 1e-12;

DiscountCurve bootstrap(const LocalDate& valuationDate,
                        const std::vector<CurveInstrumentPtr>& instruments,
                        CurveInterpolation interpolation);

/** An OisSwap has no projection dependence and is rejected here. */
DiscountCurve bootstrapProjection(const LocalDate& valuationDate,
                                  const std::vector<CurveInstrumentPtr>& instruments,
                                  CurveInterpolation interpolation,
                                  const DiscountCurve& discount);

}  // namespace multi_curve_bootstrap
}  // namespace quantfinlib
