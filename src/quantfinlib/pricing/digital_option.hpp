// Port of com.quantfinlib.pricing.DigitalOption (Java): European digital
// (binary) options under Black-Scholes. Cash-or-nothing pays a fixed
// amount if the option finishes ITM (payout*e^{-rt}*N(+-d2)); asset-or-
// nothing pays the underlying itself (S*e^{-qt}*N(+-d1)). A vanilla
// decomposes exactly into asset-or-nothing minus strike x cash-or-nothing.
// Parameter conventions match black_scholes.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace digital_option {

/** Fixed payout if spot finishes beyond the strike (call: above, put: below). */
double cashOrNothing(OptionType type, double spot, double strike,
                     double rate, double carry, double vol,
                     double timeYears, double payout);

/** Pays one unit of the underlying if the option finishes in the money. */
double assetOrNothing(OptionType type, double spot, double strike,
                      double rate, double carry, double vol, double timeYears);

}  // namespace digital_option
}  // namespace quantfinlib
