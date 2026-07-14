#include "quantfinlib/risk/var_backtest.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace var_backtest {

namespace {

/** count * ln(probability) with the 0*ln(0) = 0 convention. */
double xLnP(long long count, double probability) {
    if (count == 0) {
        return 0;
    }
    if (probability <= 0) {
        return -std::numeric_limits<double>::infinity();
    }
    return static_cast<double>(count) * std::log(probability);
}

/** chi^2(1) survival function via the normal CDF. */
double chiSquare1PValue(double statistic) {
    if (statistic <= 0) {
        return 1;
    }
    return 2 * (1 - math_utils::normCdf(std::sqrt(statistic)));
}

}  // namespace

VarBacktestResult test(const std::vector<double>& returns, double constantVar,
                       double confidence) {
    std::vector<double> var(returns.size(), constantVar);
    return test(returns, var, confidence);
}

VarBacktestResult test(const std::vector<double>& returns,
                       const std::vector<double>& varForecasts, double confidence) {
    if (returns.size() != varForecasts.size() || returns.size() < 20) {
        throw std::invalid_argument("need >= 20 aligned returns/forecasts");
    }
    if (confidence <= 0 || confidence >= 1) {
        throw std::invalid_argument("confidence must be in (0,1)");
    }
    int n = static_cast<int>(returns.size());
    std::vector<bool> exception(n);
    int x = 0;
    for (int i = 0; i < n; i++) {
        exception[i] = returns[i] < -varForecasts[i];
        if (exception[i]) {
            x++;
        }
    }
    double p = 1 - confidence;

    // Kupiec proportion-of-failures LR.
    double observedRate = static_cast<double>(x) / n;
    double llNull = xLnP(n - x, 1 - p) + xLnP(x, p);
    double llAlt = xLnP(n - x, 1 - observedRate) + xLnP(x, observedRate);
    double kupiec = std::max(0.0, 2 * (llAlt - llNull));
    double kupiecP = chiSquare1PValue(kupiec);

    // Christoffersen independence LR over exception transitions.
    long long n00 = 0, n01 = 0, n10 = 0, n11 = 0;
    for (int i = 1; i < n; i++) {
        if (!exception[i - 1] && !exception[i]) {
            n00++;
        } else if (!exception[i - 1]) {
            n01++;
        } else if (!exception[i]) {
            n10++;
        } else {
            n11++;
        }
    }
    double independence = 0;
    long long transitions = n00 + n01 + n10 + n11;
    long long exceptionsAfter = n01 + n11;
    if (transitions > 0 && exceptionsAfter > 0 && exceptionsAfter < transitions) {
        double pi = static_cast<double>(exceptionsAfter) / transitions;
        double pi01 = n00 + n01 == 0 ? 0 : static_cast<double>(n01) / (n00 + n01);
        double pi11 = n10 + n11 == 0 ? 0 : static_cast<double>(n11) / (n10 + n11);
        double llPooled = xLnP(n00 + n10, 1 - pi) + xLnP(n01 + n11, pi);
        double llMarkov = xLnP(n00, 1 - pi01) + xLnP(n01, pi01)
                + xLnP(n10, 1 - pi11) + xLnP(n11, pi11);
        independence = std::max(0.0, 2 * (llMarkov - llPooled));
    }
    double independenceP = chiSquare1PValue(independence);

    double cc = kupiec + independence;
    double ccP = std::exp(-cc / 2);  // exact chi-square(2) survival

    return {n, x, p * n, observedRate,
            kupiec, kupiecP, independence, independenceP, cc, ccP};
}

}  // namespace var_backtest
}  // namespace quantfinlib
