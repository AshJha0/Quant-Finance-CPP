#include "quantfinlib/pricing/sabr_model.hpp"

#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/pricing/java_random.hpp"

namespace quantfinlib {
namespace sabr_model {

namespace {

double sse(double f, double t, double beta, const std::vector<double>& strikes,
           const std::vector<double>& vols, double alpha, double rho, double nu) {
    double sum = 0;
    for (std::size_t i = 0; i < strikes.size(); i++) {
        double d = impliedVol(f, strikes[i], t, alpha, beta, rho, nu) - vols[i];
        sum += d * d;
    }
    return sum;
}

}  // namespace

double impliedVol(double f, double k, double t,
                  double alpha, double beta, double rho, double nu) {
    if (f <= 0 || k <= 0 || t <= 0 || alpha <= 0) {
        throw std::invalid_argument("f, k, t, alpha must be positive");
    }
    double oneMinusBeta = 1 - beta;
    double logFk = std::log(f / k);
    double fkPow = std::pow(f * k, oneMinusBeta / 2);

    // Correction term (common to ATM and non-ATM).
    double correction = 1 + t * (
            oneMinusBeta * oneMinusBeta * alpha * alpha / (24 * fkPow * fkPow)
                    + rho * beta * nu * alpha / (4 * fkPow)
                    + (2 - 3 * rho * rho) * nu * nu / 24);

    if (std::fabs(logFk) < 1e-10) {
        return alpha / std::pow(f, oneMinusBeta) * correction;
    }

    double z = nu / alpha * fkPow * logFk;
    double x = std::log((std::sqrt(1 - 2 * rho * z + z * z) + z - rho) / (1 - rho));
    double denom = fkPow * (1
            + oneMinusBeta * oneMinusBeta / 24 * logFk * logFk
            + std::pow(oneMinusBeta, 4) / 1920 * std::pow(logFk, 4));
    return alpha / denom * (z / x) * correction;
}

Params calibrate(double f, double t, double beta,
                 const std::vector<double>& strikes,
                 const std::vector<double>& marketVols) {
    if (strikes.size() != marketVols.size() || strikes.size() < 3) {
        throw std::invalid_argument("need >= 3 aligned strike/vol quotes");
    }
    // Initial alpha from the closest-to-ATM quote: vol_atm ~ alpha / f^(1-beta).
    double atmVol = marketVols[0];
    double bestDist = std::numeric_limits<double>::infinity();
    for (std::size_t i = 0; i < strikes.size(); i++) {
        double d = std::fabs(strikes[i] - f);
        if (d < bestDist) {
            bestDist = d;
            atmVol = marketVols[i];
        }
    }
    double alpha0 = atmVol * std::pow(f, 1 - beta);

    JavaSplittableRandom rnd(42);
    std::array<double, 3> best{alpha0, 0.0, 0.5};
    double bestSse = sse(f, t, beta, strikes, marketVols, best[0], best[1], best[2]);

    for (int s = 0; s < 4'000; s++) {
        // The three draws must be sequenced exactly as in Java.
        double alpha = alpha0 * (0.4 + 1.6 * rnd.nextDouble());
        double rho = -0.98 + 1.96 * rnd.nextDouble();
        double nu = 0.01 + 2.99 * rnd.nextDouble();
        double err = sse(f, t, beta, strikes, marketVols, alpha, rho, nu);
        if (err < bestSse) {
            bestSse = err;
            best = {alpha, rho, nu};
        }
    }
    // Coordinate refinement with shrinking steps.
    std::array<double, 3> steps{alpha0 * 0.1, 0.1, 0.1};
    for (int sweep = 0; sweep < 200; sweep++) {
        bool improved = false;
        for (int p = 0; p < 3; p++) {
            for (int dir = -1; dir <= 1; dir += 2) {
                std::array<double, 3> trial = best;
                trial[p] += dir * steps[p];
                if (trial[0] <= 0 || std::fabs(trial[1]) >= 0.999 || trial[2] <= 0) {
                    continue;
                }
                double err = sse(f, t, beta, strikes, marketVols,
                                 trial[0], trial[1], trial[2]);
                if (err < bestSse) {
                    bestSse = err;
                    best = trial;
                    improved = true;
                }
            }
        }
        if (!improved) {
            for (int p = 0; p < 3; p++) {
                steps[p] /= 2;
            }
            if (steps[1] < 1e-7) {
                break;
            }
        }
    }
    return Params{best[0], beta, best[1], best[2],
                  std::sqrt(bestSse / static_cast<double>(strikes.size()))};
}

}  // namespace sabr_model
}  // namespace quantfinlib
