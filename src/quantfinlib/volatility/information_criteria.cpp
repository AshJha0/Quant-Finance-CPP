#include "quantfinlib/volatility/information_criteria.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {

namespace {

void checkLogLik(double logLikelihood) {
    if (!std::isfinite(logLikelihood)) {
        throw std::invalid_argument("logLikelihood must be finite, got "
                                    + std::to_string(logLikelihood));
    }
}

void checkParams(int parameters) {
    if (parameters < 0) {
        throw std::invalid_argument("parameters must be >= 0, got "
                                    + std::to_string(parameters));
    }
}

}  // namespace

double InformationCriteria::aic(double logLikelihood, int parameters) {
    checkLogLik(logLikelihood);
    checkParams(parameters);
    return 2.0 * parameters - 2.0 * logLikelihood;
}

double InformationCriteria::bic(double logLikelihood, int parameters, int observations) {
    checkLogLik(logLikelihood);
    checkParams(parameters);
    if (observations < 1) {
        throw std::invalid_argument("observations must be >= 1, got "
                                    + std::to_string(observations));
    }
    return parameters * std::log(static_cast<double>(observations)) - 2.0 * logLikelihood;
}

}  // namespace quantfinlib
