// Port of com.quantfinlib.pricing.HigherOrderGreeks (Java): the
// second-order Greeks a vol book actually hedges with — analytic
// Black-Scholes forms. Vanna d2V/dSdVol (the skew-hedging Greek), volga
// d2V/dVol2 (vega convexity; vanna-volga charges the smile for both), and
// the Margrabe exchange-option cross-gamma d2V/dS1dS2. Same conventions
// as black_scholes: carry is the continuous yield q. Vanna/volga are
// identical for calls and puts.
#pragma once

namespace quantfinlib {
namespace higher_order_greeks {

/** Vanna d2V/dSdVol: per 1.00 spot x 1.00 vol. Same for calls and puts. */
double vanna(double spot, double strike, double rate, double carry,
             double vol, double timeYears);

/** Volga (vomma) d2V/dVol2: vega convexity. Same for calls and puts. */
double volga(double spot, double strike, double rate, double carry,
             double vol, double timeYears);

/**
 * Cross-gamma of a Margrabe exchange option (the right to exchange asset 2
 * for asset 1): d2V/dS1dS2 = -phi(d1)/(S2*sigmaHat*sqrtT) with
 * sigmaHat^2 = vol1^2 + vol2^2 - 2*rho*vol1*vol2. Negative: the exchange
 * option loses convexity when the two legs move together.
 */
double exchangeCrossGamma(double spot1, double spot2, double vol1,
                          double vol2, double correlation, double timeYears);

}  // namespace higher_order_greeks
}  // namespace quantfinlib
