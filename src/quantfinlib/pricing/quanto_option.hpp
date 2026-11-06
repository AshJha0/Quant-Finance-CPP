// Port of com.quantfinlib.pricing.QuantoOption (Java): QUANTO adjustment
// for payoffs on a foreign asset settled in domestic currency at a FIXED
// conversion rate (Hull, the quanto section of "Convexity, timing, and quanto
// adjustments"; Wystup 2006, section 2.6). Under the domestic measure the
// asset drifts at r_f - q - rho*sigmaS*sigmaFX, so
//   F_quanto = S * e^{(r_f - q - rho*sigmaS*sigmaFX)*T}
//   V = e^{-r_d T} * Black76(F_quanto, K, sigmaS, T).
// The domestic rate only discounts; the foreign rate only enters the
// forward. rho is the correlation of the asset with X = domestic per foreign.
// Pricing delegates to black_scholes with rate r_d and carry
// q + (r_d - r_f) + rho*sigmaS*sigmaFX, which reproduces that forward.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace quanto_option {

/** The quanto-adjusted forward (domestic-settled, fixed conversion). */
double quantoForward(double spot, double foreignRate, double assetYield,
                     double assetVol, double fxVol, double rho, double timeYears);

/**
 * Quanto vanilla priced in domestic currency per unit of the fixed
 * conversion rate. domesticRate discounts, foreignRate drives the forward.
 */
double price(OptionType type, double spot, double strike,
             double domesticRate, double foreignRate, double assetYield,
             double assetVol, double fxVol, double rho, double timeYears);

}  // namespace quanto_option
}  // namespace quantfinlib
