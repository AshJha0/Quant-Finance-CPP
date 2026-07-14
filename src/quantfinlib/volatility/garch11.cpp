#include "quantfinlib/volatility/garch11.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

double logLikelihood(const std::vector<double>& r, double sampleVar,
                     double alpha, double beta) {
    double omega = sampleVar * (1 - alpha - beta);
    double h = sampleVar;
    double ll = 0;
    for (double x : r) {
        if (h <= 0) {
            return -std::numeric_limits<double>::infinity();
        }
        ll += -0.5 * (std::log(2 * std::numbers::pi) + std::log(h) + x * x / h);
        h = omega + alpha * x * x + beta * h;
    }
    return ll;
}

}  // namespace

Garch11::Params Garch11::fit(const std::vector<double>& returns) {
    if (returns.size() < 100) {
        throw std::invalid_argument("need at least 100 returns, got "
                                    + std::to_string(returns.size()));
    }
    double mean = math_utils::mean(returns);
    std::vector<double> r(returns.size());
    for (std::size_t i = 0; i < r.size(); i++) {
        r[i] = returns[i] - mean;
    }
    double sampleVar = math_utils::variance(r);

    double bestAlpha = 0.05, bestBeta = 0.9;
    double bestLl = -std::numeric_limits<double>::infinity();
    // Full admissible box (the persistence check prunes invalid cells):
    // a narrower start is a hard cap refinement can barely creep past,
    // pinning low-persistence/high-ARCH series at the box edge.
    double alphaLo = 0.005, alphaHi = 0.99, betaLo = 1e-6, betaHi = 0.999;

    for (int pass = 0; pass < 3; pass++) {
        int grid = 25;
        for (int i = 0; i <= grid; i++) {
            double alpha = alphaLo + (alphaHi - alphaLo) * i / grid;
            for (int j = 0; j <= grid; j++) {
                double beta = betaLo + (betaHi - betaLo) * j / grid;
                if (alpha + beta >= 0.9995 || alpha <= 0 || beta <= 0) {
                    continue;
                }
                double ll = logLikelihood(r, sampleVar, alpha, beta);
                if (ll > bestLl) {
                    bestLl = ll;
                    bestAlpha = alpha;
                    bestBeta = beta;
                }
            }
        }
        // Zoom into the best cell for the next pass.
        double alphaStep = (alphaHi - alphaLo) / grid;
        double betaStep = (betaHi - betaLo) / grid;
        alphaLo = std::max(1e-6, bestAlpha - 2 * alphaStep);
        alphaHi = std::min(0.99, bestAlpha + 2 * alphaStep);
        betaLo = std::max(1e-6, bestBeta - 2 * betaStep);
        betaHi = std::min(0.999, bestBeta + 2 * betaStep);
    }
    double omega = sampleVar * (1 - bestAlpha - bestBeta);
    return Params{omega, bestAlpha, bestBeta, bestLl};
}

std::vector<double> Garch11::conditionalVariances(const std::vector<double>& returns,
                                                  const Params& params) {
    double mean = math_utils::mean(returns);
    std::vector<double> h(returns.size());
    h[0] = math_utils::variance(returns);
    for (std::size_t t = 1; t < returns.size(); t++) {
        double x = returns[t - 1] - mean;
        h[t] = params.omega + params.alpha * x * x + params.beta * h[t - 1];
    }
    return h;
}

double Garch11::forecastVariance(const std::vector<double>& returns,
                                 const Params& params, int horizon) {
    if (horizon < 1) {
        throw std::invalid_argument("horizon must be >= 1");
    }
    double mean = math_utils::mean(returns);
    std::vector<double> h = conditionalVariances(returns, params);
    double lastR = returns.back() - mean;
    double next = params.omega + params.alpha * lastR * lastR + params.beta * h.back();
    double uncond = params.unconditionalVariance();
    return uncond + std::pow(params.persistence(), horizon - 1) * (next - uncond);
}

}  // namespace quantfinlib
