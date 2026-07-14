// Port of com.quantfinlib.pricing.BarrierOption (Java): single-barrier
// vanilla options — continuously monitored knock-in/knock-out — for the
// REGULAR barrier configurations, priced in closed form by the reflection
// principle (Reiner-Rubinstein): down barriers on calls with H <= K,
// up barriers on puts with H >= K. Reverse barriers (barrier in the ITM
// region) need the full eight-case decomposition and are rejected
// explicitly rather than priced subtly wrong. No rebates. Conventions
// match black_scholes.
#pragma once

namespace quantfinlib {
namespace barrier_option {

/** Down-and-in call, H <= min(S, K): alive only after the barrier trades. */
double downAndInCall(double spot, double strike, double barrier,
                     double rate, double carry, double vol, double timeYears);

/** Down-and-out call, H <= min(S, K): dies if the barrier trades. */
double downAndOutCall(double spot, double strike, double barrier,
                      double rate, double carry, double vol, double timeYears);

/** Up-and-in put, H >= max(S, K): the mirror of the down-and-in call. */
double upAndInPut(double spot, double strike, double barrier,
                  double rate, double carry, double vol, double timeYears);

/** Up-and-out put, H >= max(S, K). */
double upAndOutPut(double spot, double strike, double barrier,
                   double rate, double carry, double vol, double timeYears);

}  // namespace barrier_option
}  // namespace quantfinlib
