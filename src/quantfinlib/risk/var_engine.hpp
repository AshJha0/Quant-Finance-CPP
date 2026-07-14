// Port of com.quantfinlib.risk.VarEngine (Java): portfolio Value-at-Risk,
// all four classic flavors over one input shape — factor EXPOSURES
// (currency P&L per unit factor return, a delta vector) against a factor
// covariance matrix or a factor-return history:
//   - delta-normal (variance-covariance): sigma_P = sqrt(d'Sd), VaR = z*sigma;
//   - Monte Carlo: Cholesky-correlated Gaussian factor draws (java.util.Random
//     stream via JavaRandom, deterministic per seed, matching Java exactly);
//   - delta-gamma (Cornish-Fisher), VaR and CLOSED-FORM ES;
//   - historical: replay factor-return rows through the exposures.
// Full revaluation takes the caller's pricer as std::function (the Java
// @FunctionalInterface ScenarioReval); a pricer returning NaN/Infinity
// throws std::invalid_argument. VaR and ES are POSITIVE losses in currency
// units; confidence is the one-sided level in (0.5, 1).
#pragma once

#include <cstdint>
#include <functional>
#include <vector>

namespace quantfinlib {
namespace var_engine {

using Matrix = std::vector<std::vector<double>>;

/** VaR and ES of a loss distribution, both positive. */
struct VarResult {
    double var;
    double expectedShortfall;
};

/** Revalues the book under one scenario's factor moves -> P&L (finite). */
using ScenarioReval = std::function<double(const std::vector<double>&)>;

/** Portfolio stdev sqrt(d' S d) in currency units. */
double portfolioStdev(const std::vector<double>& exposures, const Matrix& covariance);

/** Delta-normal VaR: z-quantile of the Gaussian portfolio P&L. */
double deltaNormalVar(const std::vector<double>& exposures, const Matrix& covariance,
                      double confidence);

/** Delta-normal ES: the Gaussian tail mean, sigma * phi(z) / (1 - c). */
double deltaNormalEs(const std::vector<double>& exposures, const Matrix& covariance,
                     double confidence);

/** VaR and ES from Gaussian Monte Carlo factor scenarios (>= 100). */
VarResult monteCarloVar(const std::vector<double>& exposures, const Matrix& covariance,
                        double confidence, int scenarios, std::int64_t seed);

/**
 * Second-order VaR via the Cornish-Fisher quantile of the delta-gamma
 * P&L d'Dx + 0.5 Dx'G Dx. Accurate for MODERATE gamma (skew ~< 1).
 */
double deltaGammaVar(const std::vector<double>& exposures, const Matrix& gamma,
                     const Matrix& covariance, double confidence);

/**
 * Second-order ES in CLOSED FORM: ES = -mu + sigma * phi(z)/(1-c) *
 * (1 + z*s/6) with s the loss skew; reduces exactly to deltaNormalEs
 * when gamma = 0.
 */
double deltaGammaEs(const std::vector<double>& exposures, const Matrix& gamma,
                    const Matrix& covariance, double confidence);

/**
 * Historical simulation: each row of factorReturns is one scenario
 * replayed through the exposures (>= 20 rows).
 */
VarResult historicalVar(const std::vector<double>& exposures, const Matrix& factorReturns,
                        double confidence);

/**
 * Full-revaluation VaR: every scenario repriced through the CALLER'S
 * pricer. A pricer returning NaN/Infinity throws: a scenario your
 * pricer cannot price is a modelling problem, not a quantile.
 */
VarResult fullRevaluationVar(const Matrix& scenarios, const ScenarioReval& pricer,
                             double confidence);

/**
 * VaR (quantile) and ES (tail mean) of a loss sample, positive = loss.
 * (Java package-private static; public here — FrtbEs::es975 uses it.)
 */
VarResult tail(const std::vector<double>& losses, double confidence);

}  // namespace var_engine
}  // namespace quantfinlib
