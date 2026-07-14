// Port of com.quantfinlib.pricing.BinomialTree (Java): Cox-Ross-Rubinstein
// binomial tree for European and American options with a continuous carry
// yield. Converges to Black-Scholes for European payoffs; prices the
// early-exercise premium for American ones. Degenerate trees (risk-neutral
// probability outside (0, 1)) throw rather than price nonsense.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {
namespace binomial_tree {

enum class ExerciseStyle { EUROPEAN, AMERICAN };

double price(OptionType type, ExerciseStyle style, double spot, double strike,
             double rate, double carry, double vol, double timeYears, int steps);

/** Early-exercise premium: American price minus European price. */
double earlyExercisePremium(OptionType type, double spot, double strike,
                            double rate, double carry, double vol,
                            double timeYears, int steps);

/** Delta by central difference (h = spot * 1e-4). */
double delta(OptionType type, ExerciseStyle style, double spot, double strike,
             double rate, double carry, double vol, double timeYears, int steps);

}  // namespace binomial_tree
}  // namespace quantfinlib
