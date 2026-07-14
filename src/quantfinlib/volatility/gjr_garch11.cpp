#include "quantfinlib/volatility/gjr_garch11.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

double logLikelihood(const std::vector<double>& r, double sampleVar, double alpha,
                     double gamma, double beta) {
    double omega = sampleVar * (1 - alpha - gamma / 2 - beta);
    if (omega <= 0) {
        return -std::numeric_limits<double>::infinity();
    }
    double h = sampleVar;
    double ll = 0;
    for (double x : r) {
        if (h <= 0) {
            return -std::numeric_limits<double>::infinity();
        }
        ll += -0.5 * (std::log(2 * std::numbers::pi) + std::log(h) + x * x / h);
        double arch = x < 0 ? alpha + gamma : alpha;
        h = omega + arch * x * x + beta * h;
    }
    return ll;
}

}  // namespace

GjrGarch11::Params GjrGarch11::fit(const std::vector<double>& returns) {
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

    double bestAlpha = 0.03;
    double bestGamma = 0.05;
    double bestBeta = 0.9;
    double bestLl = -std::numeric_limits<double>::infinity();
    // Initial boxes span the FULL admissible region (the persistence
    // constraint alpha + gamma/2 + beta < 1 prunes invalid cells) — a
    // narrower starting box is a hard cap the refinement passes can only
    // creep past by ~2 steps per pass, silently pinning extreme fits at
    // the box edge.
    double aLo = 0.0, aHi = 0.99;
    double gLo = 0.0, gHi = 1.9;
    double bLo = 1e-6, bHi = 0.999;

    for (int pass = 0; pass < 3; pass++) {
        int grid = 12;                 // 13^3 ~= 2,200 cells per pass
        for (int i = 0; i <= grid; i++) {
            double alpha = aLo + (aHi - aLo) * i / grid;
            for (int j = 0; j <= grid; j++) {
                double gamma = gLo + (gHi - gLo) * j / grid;
                for (int k = 0; k <= grid; k++) {
                    double beta = bLo + (bHi - bLo) * k / grid;
                    if (alpha < 0 || gamma < 0 || beta <= 0
                            || alpha + gamma / 2 + beta >= 0.9995) {
                        continue;
                    }
                    double ll = logLikelihood(r, sampleVar, alpha, gamma, beta);
                    if (ll > bestLl) {
                        bestLl = ll;
                        bestAlpha = alpha;
                        bestGamma = gamma;
                        bestBeta = beta;
                    }
                }
            }
        }
        double aStep = (aHi - aLo) / grid;
        double gStep = (gHi - gLo) / grid;
        double bStep = (bHi - bLo) / grid;
        aLo = std::max(0.0, bestAlpha - 2 * aStep);
        aHi = std::min(0.99, bestAlpha + 2 * aStep);
        gLo = std::max(0.0, bestGamma - 2 * gStep);
        gHi = std::min(1.9, bestGamma + 2 * gStep);
        bLo = std::max(1e-6, bestBeta - 2 * bStep);
        bHi = std::min(0.999, bestBeta + 2 * bStep);
    }
    double omega = sampleVar * (1 - bestAlpha - bestGamma / 2 - bestBeta);
    return Params{omega, bestAlpha, bestGamma, bestBeta, bestLl};
}

std::vector<double> GjrGarch11::conditionalVariances(const std::vector<double>& returns,
                                                     const Params& params) {
    double mean = math_utils::mean(returns);
    std::vector<double> h(returns.size());
    h[0] = math_utils::variance(returns);
    for (std::size_t t = 1; t < returns.size(); t++) {
        double x = returns[t - 1] - mean;
        double arch = x < 0 ? params.alpha + params.gamma : params.alpha;
        h[t] = params.omega + arch * x * x + params.beta * h[t - 1];
    }
    return h;
}

double GjrGarch11::forecastVariance(const std::vector<double>& returns,
                                    const Params& params, int horizon) {
    if (horizon < 1) {
        throw std::invalid_argument("horizon must be >= 1");
    }
    double mean = math_utils::mean(returns);
    std::vector<double> h = conditionalVariances(returns, params);
    double lastR = returns.back() - mean;
    double arch = lastR < 0 ? params.alpha + params.gamma : params.alpha;
    double next = params.omega + arch * lastR * lastR + params.beta * h.back();
    double uncond = params.unconditionalVariance();
    return uncond + std::pow(params.persistence(), horizon - 1) * (next - uncond);
}

}  // namespace quantfinlib
