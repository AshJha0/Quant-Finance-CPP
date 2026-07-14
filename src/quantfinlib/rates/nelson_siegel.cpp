#include "quantfinlib/rates/nelson_siegel.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace nelson_siegel {

double Fit::zeroRate(double t) const {
    if (!(t > 0) || t == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("tenor must be positive and finite, got "
                                    + std::to_string(t));
    }
    double x = t / lambda;
    double slopeLoading = (1 - std::exp(-x)) / x;
    return b0 + b1 * slopeLoading + b2 * (slopeLoading - std::exp(-x));
}

Fit fit(const std::vector<double>& tenorYears, const std::vector<double>& zeroRates) {
    std::size_t n = tenorYears.size();
    if (n < 4 || zeroRates.size() != n) {
        throw std::invalid_argument("need >= 4 aligned tenor/rate observations, got "
                + std::to_string(n) + "/" + std::to_string(zeroRates.size()));
    }
    for (std::size_t i = 0; i < n; i++) {
        if (!(tenorYears[i] > 0) || tenorYears[i] == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("tenor must be positive and finite: "
                                        + std::to_string(tenorYears[i]));
        }
        if (!std::isfinite(zeroRates[i])) {
            throw std::invalid_argument("zero rate must be finite: "
                                        + std::to_string(zeroRates[i]));
        }
    }
    double bestSse = std::numeric_limits<double>::infinity();
    Fit best{};
    bool found = false;
    int nodes = 80;
    double lo = std::log(0.1), hi = std::log(10.0);
    for (int g = 0; g < nodes; g++) {
        double lambda = std::exp(lo + (hi - lo) * g / (nodes - 1));
        // OLS in (b0,b1,b2) via 3x3 normal equations.
        math_utils::Matrix xtx(3, std::vector<double>(3, 0.0));
        std::vector<double> xty(3, 0.0);
        for (std::size_t i = 0; i < n; i++) {
            double x = tenorYears[i] / lambda;
            double f1 = (1 - std::exp(-x)) / x;
            double row[3] = {1, f1, f1 - std::exp(-x)};
            for (int a = 0; a < 3; a++) {
                xty[a] += row[a] * zeroRates[i];
                for (int b = 0; b < 3; b++) {
                    xtx[a][b] += row[a] * row[b];
                }
            }
        }
        std::vector<double> beta;
        try {
            beta = math_utils::solveLinear(xtx, xty);
        } catch (const std::invalid_argument&) {
            continue;  // degenerate node (e.g. all tenors << lambda); skip
        }
        double sse = 0;
        Fit candidate{beta[0], beta[1], beta[2], lambda, 0};
        for (std::size_t i = 0; i < n; i++) {
            double e = candidate.zeroRate(tenorYears[i]) - zeroRates[i];
            sse += e * e;
        }
        if (sse < bestSse) {
            bestSse = sse;
            best = Fit{beta[0], beta[1], beta[2], lambda, std::sqrt(sse / n)};
            found = true;
        }
    }
    if (!found) {
        throw std::invalid_argument("no lambda node produced a solvable fit");
    }
    return best;
}

}  // namespace nelson_siegel
}  // namespace quantfinlib
