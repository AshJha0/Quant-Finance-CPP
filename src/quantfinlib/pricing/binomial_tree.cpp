#include "quantfinlib/pricing/binomial_tree.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>
#include <vector>

namespace quantfinlib {
namespace binomial_tree {

double price(OptionType type, ExerciseStyle style, double spot, double strike,
             double rate, double carry, double vol, double timeYears, int steps) {
    if (steps < 1) {
        throw std::invalid_argument("steps must be >= 1");
    }
    if (timeYears <= 0) {
        return black_scholes::intrinsic(type, spot, strike);
    }
    double dt = timeYears / steps;
    double u = std::exp(vol * std::sqrt(dt));
    double d = 1 / u;
    double growth = std::exp((rate - carry) * dt);
    double p = (growth - d) / (u - d);
    if (p <= 0 || p >= 1) {
        throw std::invalid_argument(
                "degenerate tree (p=" + std::to_string(p)
                + "): increase steps or check inputs");
    }
    double discount = std::exp(-rate * dt);

    // Terminal payoffs.
    std::vector<double> values(steps + 1);
    for (int j = 0; j <= steps; j++) {
        double s = spot * std::pow(u, j) * std::pow(d, steps - j);
        values[j] = black_scholes::intrinsic(type, s, strike);
    }
    // Backward induction with optional early exercise.
    for (int i = steps - 1; i >= 0; i--) {
        for (int j = 0; j <= i; j++) {
            double continuation = discount * (p * values[j + 1] + (1 - p) * values[j]);
            if (style == ExerciseStyle::AMERICAN) {
                double s = spot * std::pow(u, j) * std::pow(d, i - j);
                values[j] = std::max(continuation,
                                     black_scholes::intrinsic(type, s, strike));
            } else {
                values[j] = continuation;
            }
        }
    }
    return values[0];
}

double earlyExercisePremium(OptionType type, double spot, double strike,
                            double rate, double carry, double vol,
                            double timeYears, int steps) {
    return price(type, ExerciseStyle::AMERICAN, spot, strike, rate, carry, vol,
                 timeYears, steps)
            - price(type, ExerciseStyle::EUROPEAN, spot, strike, rate, carry, vol,
                    timeYears, steps);
}

double delta(OptionType type, ExerciseStyle style, double spot, double strike,
             double rate, double carry, double vol, double timeYears, int steps) {
    double h = spot * 1e-4;
    double up = price(type, style, spot + h, strike, rate, carry, vol, timeYears, steps);
    double dn = price(type, style, spot - h, strike, rate, carry, vol, timeYears, steps);
    return (up - dn) / (2 * h);
}

}  // namespace binomial_tree
}  // namespace quantfinlib
