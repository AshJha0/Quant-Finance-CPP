#include "quantfinlib/risk/var_engine.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

#include "quantfinlib/risk/gaussian_copula.hpp"
#include "quantfinlib/risk/java_random.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace var_engine {

namespace {

double zOf(double confidence) {
    if (!(confidence > 0.5 && confidence < 1)) {
        throw std::invalid_argument("confidence must be in (0.5, 1)");
    }
    return math_utils::normInv(confidence);
}

int requireSquare(const std::vector<double>& exposures, const Matrix& covariance) {
    int n = static_cast<int>(exposures.size());
    if (n < 1 || static_cast<int>(covariance.size()) != n) {
        throw std::invalid_argument("covariance must match exposures");
    }
    return n;
}

Matrix multiply(const Matrix& a, const Matrix& b, int n) {
    Matrix out(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; i++) {
        for (int k = 0; k < n; k++) {
            double aik = a[i][k];
            if (aik == 0) {
                continue;
            }
            for (int j = 0; j < n; j++) {
                out[i][j] += aik * b[k][j];
            }
        }
    }
    return out;
}

double trace(const Matrix& m, int n) {
    double t = 0;
    for (int i = 0; i < n; i++) {
        t += m[i][i];
    }
    return t;
}

struct Cumulants {
    double mean;
    double variance;
    double skew;
};

/** Moments of d'Dx + 0.5 Dx'G Dx under Gaussian factors. */
Cumulants deltaGammaCumulants(const std::vector<double>& exposures, const Matrix& gamma,
                              const Matrix& covariance) {
    int n = requireSquare(exposures, covariance);
    if (static_cast<int>(gamma.size()) != n) {
        throw std::invalid_argument("gamma must match exposures");
    }
    Matrix gs = multiply(gamma, covariance, n);
    double mean = 0.5 * trace(gs, n);
    double linVar = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            linVar += exposures[i] * covariance[i][j] * exposures[j];
        }
    }
    Matrix gs2 = multiply(gs, gs, n);
    double variance = linVar + 0.5 * trace(gs2, n);
    if (variance <= 0) {
        return {mean, variance, 0};
    }
    // Third cumulant: 3 d'S G S d + tr((GS)^3).
    std::vector<double> sd(n, 0.0);
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            sd[i] += covariance[i][j] * exposures[j];
        }
    }
    double dgd = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            dgd += sd[i] * gamma[i][j] * sd[j];
        }
    }
    Matrix gs3 = multiply(gs2, gs, n);
    double kappa3 = 3 * dgd + trace(gs3, n);
    return {mean, variance, kappa3 / std::pow(variance, 1.5)};
}

}  // namespace

double portfolioStdev(const std::vector<double>& exposures, const Matrix& covariance) {
    int n = requireSquare(exposures, covariance);
    double variance = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            variance += exposures[i] * covariance[i][j] * exposures[j];
        }
    }
    return std::sqrt(std::max(variance, 0.0));
}

double deltaNormalVar(const std::vector<double>& exposures, const Matrix& covariance,
                      double confidence) {
    return zOf(confidence) * portfolioStdev(exposures, covariance);
}

double deltaNormalEs(const std::vector<double>& exposures, const Matrix& covariance,
                     double confidence) {
    double z = zOf(confidence);
    return portfolioStdev(exposures, covariance)
            * math_utils::normPdf(z) / (1 - confidence);
}

VarResult monteCarloVar(const std::vector<double>& exposures, const Matrix& covariance,
                        double confidence, int scenarios, std::int64_t seed) {
    int n = requireSquare(exposures, covariance);
    if (scenarios < 100) {
        throw std::invalid_argument("need >= 100 scenarios");
    }
    Matrix chol = GaussianCopula::cholesky(covariance);
    JavaRandom rnd(seed);
    std::vector<double> z(n);
    std::vector<double> losses(scenarios);
    for (int s = 0; s < scenarios; s++) {
        for (int i = 0; i < n; i++) {
            z[i] = rnd.nextGaussian();
        }
        double pnl = 0;
        for (int i = 0; i < n; i++) {
            double shock = 0;
            for (int j = 0; j <= i; j++) {
                shock += chol[i][j] * z[j];
            }
            pnl += exposures[i] * shock;
        }
        losses[s] = -pnl;
    }
    return tail(losses, confidence);
}

double deltaGammaVar(const std::vector<double>& exposures, const Matrix& gamma,
                     const Matrix& covariance, double confidence) {
    Cumulants c = deltaGammaCumulants(exposures, gamma, covariance);
    if (c.variance <= 0) {
        return 0;
    }
    double z = zOf(confidence);
    // Cornish-Fisher: the LOSS quantile uses -z (left tail of P&L).
    double zCf = -z + (z * z - 1) * c.skew / 6;
    double pnlQuantile = c.mean + zCf * std::sqrt(c.variance);
    return std::max(0.0, -pnlQuantile);
}

double deltaGammaEs(const std::vector<double>& exposures, const Matrix& gamma,
                    const Matrix& covariance, double confidence) {
    Cumulants c = deltaGammaCumulants(exposures, gamma, covariance);
    if (c.variance <= 0) {
        return 0;
    }
    double z = zOf(confidence);
    double lossSkew = -c.skew;
    double es = -c.mean + std::sqrt(c.variance)
            * math_utils::normPdf(z) / (1 - confidence) * (1 + z * lossSkew / 6);
    return std::max(0.0, es);
}

VarResult historicalVar(const std::vector<double>& exposures, const Matrix& factorReturns,
                        double confidence) {
    if (factorReturns.size() < 20) {
        throw std::invalid_argument("need >= 20 scenarios");
    }
    std::vector<double> losses(factorReturns.size());
    for (std::size_t s = 0; s < factorReturns.size(); s++) {
        if (factorReturns[s].size() != exposures.size()) {
            throw std::invalid_argument("scenario " + std::to_string(s)
                    + " width mismatch");
        }
        double pnl = 0;
        for (std::size_t i = 0; i < exposures.size(); i++) {
            pnl += exposures[i] * factorReturns[s][i];
        }
        losses[s] = -pnl;
    }
    return tail(losses, confidence);
}

VarResult fullRevaluationVar(const Matrix& scenarios, const ScenarioReval& pricer,
                             double confidence) {
    if (scenarios.size() < 20) {
        throw std::invalid_argument("need >= 20 scenarios");
    }
    std::vector<double> losses(scenarios.size());
    for (std::size_t s = 0; s < scenarios.size(); s++) {
        double pnl = pricer(scenarios[s]);
        if (!std::isfinite(pnl)) {
            throw std::invalid_argument(
                    "pricer returned " + std::to_string(pnl) + " for scenario "
                    + std::to_string(s) + " — a scenario the pricer cannot price "
                    + "is a modelling problem, not a quantile");
        }
        losses[s] = -pnl;
    }
    return tail(losses, confidence);
}

VarResult tail(const std::vector<double>& losses, double confidence) {
    if (!(confidence > 0.5 && confidence < 1)) {
        throw std::invalid_argument("confidence must be in (0.5, 1)");
    }
    std::vector<double> sorted = losses;
    std::sort(sorted.begin(), sorted.end());
    int n = static_cast<int>(sorted.size());
    int index = std::min(n - 1, static_cast<int>(std::ceil(confidence * n)) - 1);
    double var = std::max(0.0, sorted[index]);
    double sum = 0;
    int count = 0;
    for (int i = index; i < n; i++) {
        sum += sorted[i];
        count++;
    }
    return {var, std::max(0.0, sum / count)};
}

}  // namespace var_engine
}  // namespace quantfinlib
