#include "quantfinlib/rates/svensson.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace svensson {

double Fit::zeroRate(double t) const {
    if (!(t > 0) || t == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("tenor must be positive and finite, got "
                                    + std::to_string(t));
    }
    double x1 = t / lambda1;
    double x2 = t / lambda2;
    double f1 = (1 - std::exp(-x1)) / x1;
    double g1 = f1 - std::exp(-x1);
    double g2 = (1 - std::exp(-x2)) / x2 - std::exp(-x2);
    return b0 + b1 * f1 + b2 * g1 + b3 * g2;
}

Fit fit(const std::vector<double>& tenorYears, const std::vector<double>& zeroRates) {
    std::size_t n = tenorYears.size();
    if (n < 6 || zeroRates.size() != n) {
        throw std::invalid_argument("need >= 6 aligned tenor/rate observations, got "
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
    int nodes = 50;
    double lo = std::log(0.1), hi = std::log(10.0);
    for (int g1 = 0; g1 < nodes; g1++) {
        double lambda1 = std::exp(lo + (hi - lo) * g1 / (nodes - 1));
        for (int g2 = g1 + 1; g2 < nodes; g2++) {
            double lambda2 = std::exp(lo + (hi - lo) * g2 / (nodes - 1));
            // OLS in (b0,b1,b2,b3) via 4x4 normal equations.
            math_utils::Matrix xtx(4, std::vector<double>(4, 0.0));
            std::vector<double> xty(4, 0.0);
            for (std::size_t i = 0; i < n; i++) {
                double x1 = tenorYears[i] / lambda1;
                double x2 = tenorYears[i] / lambda2;
                double f1 = (1 - std::exp(-x1)) / x1;
                double row[4] = {1, f1, f1 - std::exp(-x1),
                                 (1 - std::exp(-x2)) / x2 - std::exp(-x2)};
                for (int a = 0; a < 4; a++) {
                    xty[a] += row[a] * zeroRates[i];
                    for (int b = 0; b < 4; b++) {
                        xtx[a][b] += row[a] * row[b];
                    }
                }
            }
            std::vector<double> beta;
            try {
                beta = math_utils::solveLinear(xtx, xty);
            } catch (const std::invalid_argument&) {
                continue;  // degenerate node (near-collinear regressors); skip
            }
            Fit candidate{beta[0], beta[1], beta[2], beta[3], lambda1, lambda2, 0};
            double sse = 0;
            for (std::size_t i = 0; i < n; i++) {
                double e = candidate.zeroRate(tenorYears[i]) - zeroRates[i];
                sse += e * e;
            }
            if (sse < bestSse) {
                bestSse = sse;
                best = Fit{beta[0], beta[1], beta[2], beta[3], lambda1, lambda2,
                           std::sqrt(sse / n)};
                found = true;
            }
        }
    }
    if (!found) {
        throw std::invalid_argument("no lambda node produced a solvable fit");
    }
    return best;
}

}  // namespace svensson
}  // namespace quantfinlib
