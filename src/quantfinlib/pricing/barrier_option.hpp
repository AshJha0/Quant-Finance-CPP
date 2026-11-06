// Port of com.quantfinlib.pricing.BarrierOption (Java): all eight single
// barrier vanillas, continuously monitored, with a rebate, in the closed form
// of Reiner and Rubinstein (1991) as tabulated in Haug, "The Complete Guide to
// Option Pricing Formulas", section 4.17. A knock-in rebate is paid at expiry
// if the barrier never trades; a knock-out rebate at the hit. Conventions
// match black_scholes: `carry` is the yield.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace barrier_option {

enum class Direction { DOWN, UP };
enum class Knock { IN, OUT };

/**
 * Any of the eight types. If the barrier is already breached a knock-in is
 * the vanilla and a knock-out is the rebate. vol must be > 0 before expiry.
 */
double price(OptionType type, Direction direction, Knock knock, double spot, double strike,
             double barrier, double rebate, double rate, double carry, double vol,
             double timeYears);

/** Down-and-in call, barrier below spot: alive only after the barrier trades. */
double downAndInCall(double spot, double strike, double barrier,
                     double rate, double carry, double vol, double timeYears);

/** Down-and-out call, barrier below spot: dies if the barrier trades. */
double downAndOutCall(double spot, double strike, double barrier,
                      double rate, double carry, double vol, double timeYears);

/** Up-and-in put, barrier above spot. */
double upAndInPut(double spot, double strike, double barrier,
                  double rate, double carry, double vol, double timeYears);

/** Up-and-out put, barrier above spot. */
double upAndOutPut(double spot, double strike, double barrier,
                   double rate, double carry, double vol, double timeYears);

}  // namespace barrier_option
}  // namespace quantfinlib
