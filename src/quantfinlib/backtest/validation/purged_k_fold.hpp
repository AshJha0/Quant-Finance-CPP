// Port of com.quantfinlib.backtest.validation.PurgedKFold: purged K-fold
// cross-validation splits with an embargo (Lopez de Prado, AFML ch. 7).
// For test fold [t0, t1) the training set is exactly
// [0, t0 - labelHorizon) U [t1 + labelHorizon + embargo, n) — purging
// removes training samples whose label window overlaps the test fold,
// the embargo buffers serial-correlation echo after it. Every fold's
// training set must be non-empty or the split refuses
// (std::invalid_argument).
#pragma once

#include <vector>

namespace quantfinlib {
namespace purged_k_fold {

/** One fold: test on [testFrom, testTo), train on trainIndices (ascending). */
struct Split {
    int fold = 0;
    int testFrom = 0;
    int testTo = 0;
    std::vector<int> trainIndices;
};

/**
 * @param n            number of samples, >= 2k
 * @param k            number of folds, >= 2
 * @param labelHorizon bars each label looks ahead (>= 0)
 * @param embargo      extra bars dropped after the post-test purge zone (>= 0)
 */
std::vector<Split> splits(int n, int k, int labelHorizon, int embargo);

}  // namespace purged_k_fold
}  // namespace quantfinlib
