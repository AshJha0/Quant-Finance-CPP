// Port of com.quantfinlib.pricing.QuantoOption (Java): QUANTO adjustment
// for payoffs on a foreign asset settled in domestic currency at a FIXED
// conversion rate. The hedger's FX-correlated hedge P&L shows up as a
// drift correction: F_quanto = S * e^{(r_dom - q - rho*sigmaS*sigmaFX)*T}.
// Pricing delegates to black_scholes with the carry bumped by
// rho*sigmaS*sigmaFX — one line of new math, every edge case inherited.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace quanto_option {

/** The quanto-adjusted forward (domestic-settled, fixed conversion). */
double quantoForward(double spot, double domesticRate, double assetYield,
                     double assetVol, double fxVol, double rho, double timeYears);

/**
 * Quanto vanilla priced in domestic currency per unit of the fixed
 * conversion rate: Black-Scholes with the carry shifted by rho*sigmaS*sigmaFX.
 */
double price(OptionType type, double spot, double strike,
             double domesticRate, double assetYield,
             double assetVol, double fxVol, double rho, double timeYears);

}  // namespace quanto_option
}  // namespace quantfinlib
