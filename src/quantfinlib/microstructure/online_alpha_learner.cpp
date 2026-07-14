#include "quantfinlib/microstructure/online_alpha_learner.hpp"

#include <cmath>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

OnlineAlphaLearner::OnlineAlphaLearner(double learningRate, double ridgeLambda, double icAlpha)
        : learningRate_(learningRate), ridgeLambda_(ridgeLambda), icAlpha_(icAlpha) {
    if (learningRate <= 0 || ridgeLambda < 0 || icAlpha <= 0 || icAlpha > 1) {
        throw std::invalid_argument("need learningRate > 0, ridgeLambda >= 0, icAlpha in (0,1]");
    }
}

double OnlineAlphaLearner::predict(double queueImbalance, double tradeImbalance,
                                   double normalizedOfi, double momentumZ) const {
    return w_[0] * queueImbalance + w_[1] * tradeImbalance + w_[2] * normalizedOfi
            + w_[3] * momentumZ;
}

void OnlineAlphaLearner::train(double queueImbalance, double tradeImbalance, double normalizedOfi,
                               double momentumZ, double realizedReturn) {
    if (!std::isfinite(queueImbalance) || !std::isfinite(tradeImbalance)
        || !std::isfinite(normalizedOfi) || !std::isfinite(momentumZ)
        || !std::isfinite(realizedReturn)) {
        return;
    }
    double pred = predict(queueImbalance, tradeImbalance, normalizedOfi, momentumZ);
    meanPred_ += icAlpha_ * (pred - meanPred_);
    meanRet_ += icAlpha_ * (realizedReturn - meanRet_);
    double dp = pred - meanPred_;
    double dr = realizedReturn - meanRet_;
    varPred_ += icAlpha_ * (dp * dp - varPred_);
    varRet_ += icAlpha_ * (dr * dr - varRet_);
    covar_ += icAlpha_ * (dp * dr - covar_);
    double ap = std::abs(pred);
    absPredEwma_ = absPredEwma_ == 0 ? ap : absPredEwma_ + icAlpha_ * (ap - absPredEwma_);
    samples_++;

    double err = realizedReturn - pred;
    w_[0] += learningRate_ * (err * queueImbalance - ridgeLambda_ * w_[0]);
    w_[1] += learningRate_ * (err * tradeImbalance - ridgeLambda_ * w_[1]);
    w_[2] += learningRate_ * (err * normalizedOfi - ridgeLambda_ * w_[2]);
    w_[3] += learningRate_ * (err * momentumZ - ridgeLambda_ * w_[3]);
}

double OnlineAlphaLearner::outOfSampleIC() const {
    double denom = std::sqrt(varPred_ * varRet_);
    return denom > 0 ? covar_ / denom : 0;
}

double OnlineAlphaLearner::normalizedPrediction(double queueImbalance, double tradeImbalance,
                                                double normalizedOfi, double momentumZ) const {
    if (static_cast<double>(samples_) * icAlpha_ < 1 || outOfSampleIC() <= 0 || absPredEwma_ <= 0) {
        return 0;
    }
    double pred = predict(queueImbalance, tradeImbalance, normalizedOfi, momentumZ);
    return math_utils::clamp(pred / (2 * absPredEwma_), -1, 1);
}

}  // namespace quantfinlib
