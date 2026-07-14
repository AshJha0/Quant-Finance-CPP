// Port of com.quantfinlib.pricing.TouchOption (Java): one-touch and
// no-touch options (pay-at-expiry) under continuously monitored GBM, with
// the barrier-hitting probability itself exposed. With log-drift
// m = r - q - vol^2/2 and h = ln(H/S), the reflection principle gives
// P(touch upper) = N((-h + mT)/(vol sqrtT)) + e^{2mh/vol^2} N((-h - mT)/(vol sqrtT)),
// symmetrically for a lower barrier. Conventions match black_scholes.
#pragma once

namespace quantfinlib {
namespace touch_option {

/**
 * Probability that spot touches the barrier at least once before
 * timeYears, under GBM with the given rate/carry/vol.
 */
double hitProbability(double spot, double barrier, double rate,
                      double carry, double vol, double timeYears);

/** Pays payout at expiry if the barrier traded at any point. */
double oneTouch(double spot, double barrier, double rate, double carry,
                double vol, double timeYears, double payout);

/** Pays payout at expiry if the barrier never traded. */
double noTouch(double spot, double barrier, double rate, double carry,
               double vol, double timeYears, double payout);

}  // namespace touch_option
}  // namespace quantfinlib
