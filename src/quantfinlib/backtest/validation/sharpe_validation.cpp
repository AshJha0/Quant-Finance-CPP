#include "quantfinlib/backtest/validation/sharpe_validation.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace sharpe_validation {

namespace {
constexpr double EULER_GAMMA = 0.5772156649015329;
constexpr double MATH_E = 2.718281828459045;  // Java Math.E
}  // namespace

double probabilisticSharpe(double observedSharpe, double benchmarkSharpe,
                           int nObs, double skewness, double kurtosis) {
    if (nObs < 2) {
        throw std::invalid_argument("need at least 2 observations");
    }
    double variance = 1 - skewness * observedSharpe
            + (kurtosis - 1) / 4.0 * observedSharpe * observedSharpe;
    if (variance <= 0) {
        return observedSharpe > benchmarkSharpe ? 1 : 0;
    }
    double z = (observedSharpe - benchmarkSharpe) * std::sqrt(nObs - 1.0)
            / std::sqrt(variance);
    return math_utils::normCdf(z);
}

double expectedMaxSharpe(int trials, double varianceOfTrialSharpes) {
    if (trials < 2) {
        throw std::invalid_argument("need at least 2 trials");
    }
    double sd = std::sqrt(std::max(0.0, varianceOfTrialSharpes));
    return sd * ((1 - EULER_GAMMA) * math_utils::normInv(1 - 1.0 / trials)
            + EULER_GAMMA * math_utils::normInv(1 - 1.0 / (trials * MATH_E)));
}

double deflatedSharpe(double observedSharpe, const std::vector<double>& trialSharpes,
                      int nObs, double skewness, double kurtosis) {
    double benchmark = expectedMaxSharpe(static_cast<int>(trialSharpes.size()),
                                         math_utils::variance(trialSharpes));
    return probabilisticSharpe(observedSharpe, benchmark, nObs, skewness, kurtosis);
}

double minTrackRecordLength(double observedSharpe, double benchmarkSharpe,
                            double skewness, double kurtosis, double confidence) {
    if (!(confidence > 0 && confidence < 1)) {
        throw std::invalid_argument("confidence must be in (0, 1)");
    }
    if (!std::isfinite(observedSharpe) || !std::isfinite(benchmarkSharpe)
            || !std::isfinite(skewness) || !std::isfinite(kurtosis)) {
        throw std::invalid_argument("inputs must be finite");
    }
    if (observedSharpe <= benchmarkSharpe) {
        return std::numeric_limits<double>::infinity();
    }
    double variance = 1 - skewness * observedSharpe
            + (kurtosis - 1) / 4.0 * observedSharpe * observedSharpe;
    if (variance <= 0) {
        return 2;  // PSR is already 1: any record suffices
    }
    double z = math_utils::normInv(confidence);
    double edge = observedSharpe - benchmarkSharpe;
    return 1 + variance * z * z / (edge * edge);
}

}  // namespace sharpe_validation
}  // namespace quantfinlib
