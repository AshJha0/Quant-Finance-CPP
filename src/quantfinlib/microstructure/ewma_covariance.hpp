// Port of com.quantfinlib.microstructure.EwmaCovariance: streaming EWMA
// covariance matrix, cov <- lambda*cov + (1-lambda)*ri*rj, zero-mean
// convention. The matrix stays PSD because every update is a full-vector
// rank-1 outer product -- a sample with ANY non-finite return is dropped
// whole. Lower triangle stored flat. Persistence (persist::Checkpoint):
// writeState/readState persist the learned covariance matrix and sample
// count; format version 1, matching Java's EwmaCovariance.writeState/
// readState byte for byte.
#pragma once

#include <vector>

#include "quantfinlib/persist/checkpoint.hpp"

namespace quantfinlib {

class EwmaCovariance {
public:
    /**
     * @param symbols basket size (dense indices, fixed at construction)
     * @param lambda  decay per sample, e.g. 0.94 (RiskMetrics daily convention)
     */
    explicit EwmaCovariance(int symbols, double lambda = 0.94);

    /** One sampling interval: every symbol's return over the interval that just closed. */
    void onReturns(const std::vector<double>& returns);

    /** Decayed covariance between two symbols (order-free). */
    double covariance(int i, int j) const;

    /** Decayed variance of one symbol. */
    double variance(int i) const;

    /** Decayed volatility (per sqrt(interval)), 0 until learned. */
    double volatility(int i) const;

    /** Decayed correlation in [-1, 1]; 0 while either variance is 0. */
    double correlation(int i, int j) const;

    /** w'*Sigma*w: portfolio variance of the (signed) weight vector. */
    double portfolioVariance(const std::vector<double>& weights) const;

    /**
     * Marginal contribution to portfolio risk (out[i] = wi*(Sigma*w)i / (w'Sigma*w)).
     * Returns the portfolio variance w'Sigma*w.
     */
    double marginalContribution(const std::vector<double>& weights, std::vector<double>& out) const;

    /** Live minimum-variance hedge ratio: cov(target,hedge)/var(hedge). 0 if hedge var unlearned. */
    double minVarianceHedgeRatio(int target, int hedge) const;

    int symbols() const { return symbols_; }
    long samples() const { return samples_; }

    /** Persists the learned matrix -- see persist::Checkpoint. */
    void writeState(BinWriter& out) const;

    /** Restores the matrix. Throws std::runtime_error on a basket-size or version mismatch. */
    void readState(BinReader& in);

private:
    void requireLength(const std::vector<double>& a) const;

    int symbols_;
    double lambda_;
    std::vector<double> tri_;
    long samples_ = 0;
};

}  // namespace quantfinlib
