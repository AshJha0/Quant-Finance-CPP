#include "quantfinlib/volatility/har_rv.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {
constexpr int WEEK = 5;
constexpr int MONTH = 22;
}  // namespace

HarRv::Params HarRv::fit(const std::vector<double>& realizedVariance) {
    int n = static_cast<int>(realizedVariance.size());
    if (n < 60) {
        throw std::invalid_argument("need >= 60 daily observations, got "
                                    + std::to_string(n));
    }
    for (double rv : realizedVariance) {
        if (!(rv >= 0) || rv == std::numeric_limits<double>::infinity()) {
            throw std::invalid_argument("realized variance must be >= 0 and finite");
        }
    }
    // Rows t = MONTH-1 .. n-2 predict rv[t+1].
    int rows = n - MONTH;
    math_utils::Matrix xtx(4, std::vector<double>(4, 0.0));
    std::vector<double> xty(4, 0.0);
    double x[4];
    for (int t = MONTH - 1; t < n - 1; t++) {
        x[0] = 1;
        x[1] = realizedVariance[t];
        x[2] = math_utils::mean(realizedVariance, t - WEEK + 1, t + 1);
        x[3] = math_utils::mean(realizedVariance, t - MONTH + 1, t + 1);
        for (int i = 0; i < 4; i++) {
            for (int j = 0; j < 4; j++) {
                xtx[i][j] += x[i] * x[j];
            }
            xty[i] += x[i] * realizedVariance[t + 1];
        }
    }
    if (rows < 30) {
        throw std::invalid_argument("only " + std::to_string(rows) + " regression rows");
    }
    std::vector<double> beta = math_utils::solveLinear(xtx, xty);
    return Params{beta[0], beta[1], beta[2], beta[3]};
}

double HarRv::forecast(const std::vector<double>& realizedVariance, const Params& p) {
    int n = static_cast<int>(realizedVariance.size());
    if (n < MONTH) {
        throw std::invalid_argument("need >= " + std::to_string(MONTH)
                                    + " observations to forecast");
    }
    double d = realizedVariance[n - 1];
    double w = math_utils::mean(realizedVariance, n - WEEK, n);
    double m = math_utils::mean(realizedVariance, n - MONTH, n);
    return std::max(0.0, p.intercept
                             + p.betaDaily * d + p.betaWeekly * w + p.betaMonthly * m);
}

}  // namespace quantfinlib
