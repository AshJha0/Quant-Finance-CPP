// Port of com.quantfinlib.risk.ExtremeValueTheory (Java): peaks-over-
// threshold EVT. Fits a Generalized Pareto Distribution to the losses
// STRICTLY above a threshold quantile via probability-weighted moments
// (Hosking & Wallis 1987, closed form), then extrapolates:
//   VaR_p = u + (beta/xi) * [((n/Nu)(1-p))^{-xi} - 1]
// expectedShortfall REFUSES (std::runtime_error) when xi >= 1: the
// fitted tail has no finite mean. Ties equal to the threshold are not
// exceedances (they would deflate both PWMs and bias the shape).
#pragma once

#include <vector>

namespace quantfinlib {
namespace extreme_value_theory {

/**
 * A fitted POT tail model.
 * threshold:   u — losses above this were fitted
 * shape:       xi — the tail index (fat when positive)
 * scale:       beta — the GPD scale
 * exceedances: how many losses exceeded u
 * sampleSize:  total losses the tail sits in
 */
struct GpdFit {
    double threshold;
    double shape;
    double scale;
    int exceedances;
    int sampleSize;

    /** Tail VaR at one-sided confidence p (must lie in the tail). */
    double var(double p) const;

    /**
     * Tail expected shortfall at p. Refuses (std::runtime_error) when
     * xi >= 1: the fitted tail's mean is infinite.
     */
    double expectedShortfall(double p) const;
};

/**
 * Fits a GPD to the losses exceeding the thresholdQuantile of the
 * sample (e.g. 0.90), via probability-weighted moments. Losses are
 * positive numbers (feed -returns or a loss series directly).
 */
GpdFit fitPot(const std::vector<double>& losses, double thresholdQuantile);

}  // namespace extreme_value_theory
}  // namespace quantfinlib
