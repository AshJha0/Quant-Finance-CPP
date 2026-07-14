// Port of com.quantfinlib.alpha.AlphaValidation: overfitting defense --
// walk-forward selection (model selection strictly inside the training
// window; the leak-fixed forward window containment), blocked k-fold
// cross-validation, Monte Carlo permutation robustness, and parameter
// sensitivity.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"

namespace quantfinlib {
namespace alpha_validation {

/** One walk-forward fold: what was chosen, and how it did out of sample. */
struct Fold {
    int trainStart = 0;
    int testStart = 0;
    int testEnd = 0;
    std::string chosenFactor;
    double inSampleIc = 0;
    double outOfSampleIc = 0;
};

/** All folds plus the aggregate in-sample vs out-of-sample comparison. */
struct WalkForwardResult {
    std::vector<Fold> folds;
    double meanInSampleIc = 0;
    double meanOutOfSampleIc = 0;

    /** OOS/IS efficiency: below ~0.5 the selection is mostly fitting noise. */
    double efficiency() const { return meanInSampleIc == 0 ? 0 : meanOutOfSampleIc / meanInSampleIc; }
};

/**
 * Rolls a train/test split across the sample: each fold picks the
 * candidate with the best training-window mean IC and scores it on the
 * next testBars unseen bars. Model selection happens strictly inside the
 * training window (the forward window of a selection date must fit
 * before testStart) -- the leak-fixed forward-window containment.
 */
WalkForwardResult walkForward(const AlphaContext& ctx, const std::vector<AlphaFactor>& candidates,
                               int horizon, int startIndex, int trainBars, int testBars);

/** Per-block ICs with their dispersion -- consistency across regimes. */
struct CrossValidationResult {
    std::vector<double> blockIcs;
    double meanIc = 0;
    double icStd = 0;

    /** Fraction of blocks where the IC kept the overall sign. */
    double signConsistency() const;
};

/** Splits the evaluation range into k contiguous blocks and recomputes the mean IC inside each. */
CrossValidationResult crossValidate(const AlphaContext& ctx, const AlphaFactor& factor, int horizon,
                                     int startIndex, int k);

/** Observed mean IC against its permutation null distribution. */
struct RobustnessResult {
    double observedMeanIc = 0;
    double pValue = 0;
    double nullMean = 0;
    double nullStd = 0;
    int trials = 0;
};

/**
 * Permutation test on the score/return pairing: destroys any true
 * predictive link by re-pairing scores at date i with forward returns
 * from a shuffled date, preserving both marginal distributions.
 */
RobustnessResult monteCarloRobustness(const AlphaContext& ctx, const AlphaFactor& factor, int horizon,
                                       int startIndex, int trials, std::int64_t seed);

/** IC across the sweep plus the worst adjacent-parameter drop. */
struct SensitivityResult {
    std::vector<std::string> names;
    std::vector<double> meanIcs;
    double worstNeighborDrop = 0;

    /** The candidate with the best mean IC. */
    std::string best() const;
};

/**
 * Evaluates each candidate (an ORDERED parameter sweep) and reports the
 * worst IC drop between adjacent candidates.
 */
SensitivityResult parameterSensitivity(const AlphaContext& ctx, const std::vector<AlphaFactor>& sweep,
                                        int horizon, int startIndex);

/**
 * Mean rank IC over [from, toExclusive), stepping by the horizon. The
 * ENTIRE forward window (t, t+horizon] must fit inside the range.
 */
double meanIc(const AlphaContext& ctx, const AlphaFactor& factor, int from, int toExclusive,
               int horizon);

}  // namespace alpha_validation
}  // namespace quantfinlib
