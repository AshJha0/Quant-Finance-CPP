// Port of com.quantfinlib.pricing.AsianOption (Java): ASIAN
// (average-price) options. Geometric average — exact Kemna-Vorst (1990)
// discrete-fixing closed form (a product of lognormals is lognormal);
// n = 1 collapses to vanilla Black-Scholes exactly. Arithmetic average —
// Turnbull-Wakeman (1991) two-moment lognormal matching with EXACT first
// two moments under GBM (the double sum is O(n^2)). AM-GM guarantees the
// arithmetic call is worth at least the geometric one. Fixings are
// equally spaced at t_i = i T / n (last fixing AT expiry); a seasoned
// Asian must be decomposed before calling.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace asian_option {

/**
 * Exact discrete geometric-average Asian price (Kemna-Vorst).
 * averagingPoints is the number of equally spaced fixings n >= 1;
 * n = 1 is vanilla BS.
 */
double geometricPrice(OptionType type, double spot, double strike,
                      double rate, double carry, double vol, double timeYears,
                      int averagingPoints);

/**
 * Arithmetic-average Asian price via Turnbull-Wakeman two-moment
 * lognormal matching (O(n^2) in the fixing count).
 */
double arithmeticPrice(OptionType type, double spot, double strike,
                       double rate, double carry, double vol, double timeYears,
                       int averagingPoints);

}  // namespace asian_option
}  // namespace quantfinlib
