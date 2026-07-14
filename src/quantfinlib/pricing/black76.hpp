// Port of com.quantfinlib.pricing.Black76 (Java): the Black-Scholes sibling
// for options on FORWARDS and futures (caps/floors, swaptions, commodity
// futures options). A forward has no carry, so the price is the discounted
// Black formula on the forward itself. Equivalent to BlackScholes with
// carry = 0 and spot = forward (a test pins that identity), but the market
// quotes these instruments in Black-76 terms, so the model has its own
// front door. NaN-transparent inputs produce NaN outputs.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace black76 {

/** Discounted Black-76 price of a call/put on a forward. */
double price(OptionType type, double forward, double strike,
             double rate, double vol, double timeYears);

/** Sensitivity to the FORWARD (not spot): df*N(d1) for calls. */
double delta(OptionType type, double forward, double strike,
             double rate, double vol, double timeYears);

/** Vega per 1.00 of vol (divide by 100 for per-point). Same for calls and puts. */
double vega(double forward, double strike, double rate,
            double vol, double timeYears);

/** Black-76 implied vol from a price, via bisection (NaN if unattainable). */
double impliedVol(OptionType type, double marketPrice, double forward,
                  double strike, double rate, double timeYears);

}  // namespace black76
}  // namespace quantfinlib
