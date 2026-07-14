// Port of com.quantfinlib.microstructure.OnlineAlphaLearner: online ridge
// regression (SGD with L2 shrinkage) from four dimensionless signal
// ingredients to the next-interval return, with a prequential
// out-of-sample IC gate (predict-then-train: train() scores the
// prediction made with the CURRENT weights before the realized return
// updates them). Skipped vs. the Java source: predictFrom/trainFrom(
// SignalEngine, ...) (SignalEngine is a bus/feed-coupled class out of
// this phase's scope) and the persist.Checkpoint writeState/readState
// (no C++ port of that serialization framework yet).
#pragma once

#include <cstdint>

namespace quantfinlib {

class OnlineAlphaLearner {
public:
    /**
     * @param learningRate SGD step, e.g. 0.01
     * @param ridgeLambda  L2 shrinkage toward 0 per step, e.g. 1e-4
     * @param icAlpha      EWMA weight of the IC statistics, e.g. 0.01
     */
    explicit OnlineAlphaLearner(double learningRate = 0.01, double ridgeLambda = 1e-4,
                                 double icAlpha = 0.01);

    /** The learned prediction of the next-interval return from the four ingredients. */
    double predict(double queueImbalance, double tradeImbalance, double normalizedOfi,
                    double momentumZ) const;

    /**
     * One learning step: scores the prediction made with the CURRENT weights
     * against realizedReturn, then updates the weights by ridge-SGD.
     * Non-finite inputs are skipped entirely.
     */
    void train(double queueImbalance, double tradeImbalance, double normalizedOfi,
               double momentumZ, double realizedReturn);

    /** The prequential (out-of-sample) information coefficient; 0 before enough variance. */
    double outOfSampleIC() const;

    /**
     * The prediction scaled by its own typical magnitude and clamped to
     * [-1, 1]; 0 while the OOS IC is not positive or the track record is
     * shorter than one IC memory.
     */
    double normalizedPrediction(double queueImbalance, double tradeImbalance, double normalizedOfi,
                                 double momentumZ) const;

    /** The learned weight for feature i (0=queueImb, 1=tradeImb, 2=OFI, 3=momZ). */
    double weight(int i) const { return w_[static_cast<std::size_t>(i)]; }

    long samples() const { return samples_; }

private:
    static constexpr int kFeatures = 4;

    double learningRate_;
    double ridgeLambda_;
    double icAlpha_;

    double w_[kFeatures] = {0, 0, 0, 0};

    double meanPred_ = 0;
    double meanRet_ = 0;
    double varPred_ = 0;
    double varRet_ = 0;
    double covar_ = 0;
    double absPredEwma_ = 0;
    long samples_ = 0;
};

}  // namespace quantfinlib
