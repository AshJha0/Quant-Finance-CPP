// Port of com.quantfinlib.microstructure.AlphaEnsemble: IC-weighted alpha
// ensemble. Runs one prequential IC per component: onObservation scores
// the component values snapshotted at the PREVIOUS call against the
// return just realized, then snapshots the current values. combined()
// weights each component by max(0, IC) and the weights are DELIBERATELY
// NOT renormalized (preserved from the Java source -- a barely-trusted
// blend must be a barely-sized signal). Skipped vs. the Java source: the
// persist.Checkpoint-based writeState/readState (that serialization
// framework has no C++ port in this phase).
#pragma once

#include <vector>

namespace quantfinlib {

class AlphaEnsemble {
public:
    /**
     * @param components number of component signals (fixed order)
     * @param icAlpha    EWMA weight of the IC statistics, e.g. 0.01
     */
    explicit AlphaEnsemble(int components, double icAlpha = 0.01);

    /**
     * One interval: current component values and the return realized since
     * the previous call. Scores the PREVIOUS snapshot against the return,
     * then snapshots `values`. The first call only snapshots.
     */
    void onObservation(const std::vector<double>& values, double realizedReturn);

    /** The blended alpha in [-1, 1]. */
    double combined(const std::vector<double>& values) const;

    /** The prequential (out-of-sample) IC of one component; 0 before enough variance. */
    double componentIC(int c) const;

    int components() const { return components_; }
    long samples() const { return samples_; }

private:
    void requireLength(const std::vector<double>& a) const;

    int components_;
    double icAlpha_;

    std::vector<double> meanSig_;
    std::vector<double> meanRet_;
    std::vector<double> varSig_;
    std::vector<double> varRet_;
    std::vector<double> covar_;

    std::vector<double> snapshot_;
    bool hasSnapshot_ = false;
    long samples_ = 0;
};

}  // namespace quantfinlib
