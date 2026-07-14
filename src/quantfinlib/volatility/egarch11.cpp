#include "quantfinlib/volatility/egarch11.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

const double E_ABS_Z = std::sqrt(2 / std::numbers::pi);

/**
 * The recursion needs a positive, finite starting variance — a constant or
 * NaN-bearing series would return h = 0 or NaN silently, contradicting the
 * "positive by construction" guarantee.
 */
void requireSeries(const std::vector<double>& returns) {
    if (returns.size() < 2) {
        throw std::invalid_argument("need >= 2 returns");
    }
    for (double x : returns) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
    if (!(quantfinlib::math_utils::variance(returns) > 0)) {
        throw std::invalid_argument("returns carry no variance");
    }
}

double logLikelihood(const std::vector<double>& r, double lnSampleVar,
                     double alpha, double gamma, double beta) {
    double lnH = lnSampleVar;
    double omega = (1 - beta) * lnSampleVar;
    double ll = 0;
    for (double x : r) {
        // A runaway log-variance means these parameters are junk for
        // this data — reject the cell rather than overflow exp().
        if (!std::isfinite(lnH) || std::abs(lnH) > 100) {
            return -std::numeric_limits<double>::infinity();
        }
        double h = std::exp(lnH);
        ll += -0.5 * (std::log(2 * std::numbers::pi) + lnH + x * x / h);
        double z = x / std::sqrt(h);
        lnH = omega + beta * lnH + alpha * (std::abs(z) - E_ABS_Z) + gamma * z;
    }
    return ll;
}

}  // namespace

Egarch11::Params Egarch11::fit(const std::vector<double>& returns) {
    if (returns.size() < 100) {
        throw std::invalid_argument("need at least 100 returns, got "
                                    + std::to_string(returns.size()));
    }
    for (double x : returns) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
    double mean = math_utils::mean(returns);
    std::vector<double> r(returns.size());
    for (std::size_t i = 0; i < r.size(); i++) {
        r[i] = returns[i] - mean;
    }
    double sampleVar = math_utils::variance(r);
    if (!(sampleVar > 0)) {
        throw std::invalid_argument("returns carry no variance");
    }
    double lnVar = std::log(sampleVar);

    double bestAlpha = 0.1;
    double bestGamma = 0;
    double bestBeta = 0.9;
    double bestLl = -std::numeric_limits<double>::infinity();
    // The empirically plausible box (see class doc): |beta| < 1 is the
    // only formal constraint, but negative alpha/beta describe oscillating
    // log-variance no return series exhibits and are not searched.
    double aLo = 0.0, aHi = 0.9;
    double gLo = -0.9, gHi = 0.9;
    double bLo = 0.0, bHi = 0.995;

    for (int pass = 0; pass < 3; pass++) {
        int grid = 12;
        for (int i = 0; i <= grid; i++) {
            double alpha = aLo + (aHi - aLo) * i / grid;
            for (int j = 0; j <= grid; j++) {
                double gamma = gLo + (gHi - gLo) * j / grid;
                for (int k = 0; k <= grid; k++) {
                    double beta = bLo + (bHi - bLo) * k / grid;
                    double ll = logLikelihood(r, lnVar, alpha, gamma, beta);
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
        aHi = std::min(0.9, bestAlpha + 2 * aStep);
        gLo = std::max(-0.9, bestGamma - 2 * gStep);
        gHi = std::min(0.9, bestGamma + 2 * gStep);
        bLo = std::max(0.0, bestBeta - 2 * bStep);
        bHi = std::min(0.995, bestBeta + 2 * bStep);
    }
    double omega = (1 - bestBeta) * lnVar;
    return Params{omega, bestAlpha, bestGamma, bestBeta, bestLl};
}

std::vector<double> Egarch11::conditionalVariances(const std::vector<double>& returns,
                                                   const Params& p) {
    requireSeries(returns);
    double mean = math_utils::mean(returns);
    std::vector<double> h(returns.size());
    double lnH = std::log(math_utils::variance(returns));
    h[0] = std::exp(lnH);
    for (std::size_t t = 1; t < returns.size(); t++) {
        double x = returns[t - 1] - mean;
        double z = x / std::sqrt(h[t - 1]);
        lnH = p.omega + p.beta * lnH + p.alpha * (std::abs(z) - E_ABS_Z) + p.gamma * z;
        h[t] = std::exp(lnH);
    }
    return h;
}

double Egarch11::nextVariance(const std::vector<double>& returns, const Params& p) {
    std::vector<double> h = conditionalVariances(returns, p);
    double mean = math_utils::mean(returns);
    double x = returns.back() - mean;
    double last = h.back();
    double z = x / std::sqrt(last);
    return std::exp(p.omega + p.beta * std::log(last)
                    + p.alpha * (std::abs(z) - E_ABS_Z) + p.gamma * z);
}

}  // namespace quantfinlib
