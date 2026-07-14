#include "quantfinlib/alpha/alpha_validation.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/alpha/java_random_int.hpp"
#include "quantfinlib/alpha/signal_evaluator.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace alpha_validation {

namespace {

double kNaN = std::numeric_limits<double>::quiet_NaN();

/** Evaluation dates: startIndex, stepping by horizon, forward window in-sample. */
std::vector<int> grid(const AlphaContext& ctx, int startIndex, int horizon) {
    std::vector<int> dates;
    for (int t = startIndex; t + horizon < ctx.bars(); t += horizon) {
        dates.push_back(t);
    }
    return dates;
}

/** Per-candidate IC at every grid date, computed once. */
std::vector<std::vector<double>> icMatrix(const AlphaContext& ctx,
                                          const std::vector<AlphaFactor>& candidates,
                                          const std::vector<int>& dates, int horizon) {
    std::vector<std::vector<double>> ic(candidates.size(), std::vector<double>(dates.size()));
    for (std::size_t d = 0; d < dates.size(); d++) {
        std::vector<double> fwd = signal_evaluator::forwardReturns(ctx, dates[d], horizon);
        for (std::size_t c = 0; c < candidates.size(); c++) {
            ic[c][d] = signal_evaluator::spearman(candidates[c].scores(ctx, dates[d]), fwd);
        }
    }
    return ic;
}

/** Mean IC over grid dates whose whole forward window fits in [from, to). */
double windowMean(const std::vector<double>& ic, const std::vector<int>& dates, int from, int to,
                  int horizon) {
    double sum = 0;
    int n = 0;
    for (std::size_t d = 0; d < dates.size(); d++) {
        int t = dates[d];
        if (t >= from && t + horizon < to && !std::isnan(ic[d])) {
            sum += ic[d];
            n++;
        }
    }
    return n == 0 ? kNaN : sum / n;
}

std::vector<int> identity(int n) {
    std::vector<int> a(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        a[static_cast<std::size_t>(i)] = i;
    }
    return a;
}

/** Fisher-Yates in place, matching java.util.Random-driven shuffle order. */
void shuffle(std::vector<int>& a, alpha_detail::JavaRandomInt& rng) {
    for (int i = static_cast<int>(a.size()) - 1; i > 0; i--) {
        int j = rng.nextInt(i + 1);
        std::swap(a[static_cast<std::size_t>(i)], a[static_cast<std::size_t>(j)]);
    }
}

double meanIcOf(const std::vector<std::vector<double>>& scores,
                const std::vector<std::vector<double>>& forwards, const std::vector<int>& pairing) {
    double sum = 0;
    int n = 0;
    for (std::size_t i = 0; i < pairing.size(); i++) {
        double ic = signal_evaluator::spearman(scores[i], forwards[static_cast<std::size_t>(pairing[i])]);
        if (!std::isnan(ic)) {
            sum += ic;
            n++;
        }
    }
    return n == 0 ? kNaN : sum / n;
}

}  // namespace

double meanIc(const AlphaContext& ctx, const AlphaFactor& factor, int from, int toExclusive,
              int horizon) {
    double sum = 0;
    int n = 0;
    int end = std::min(toExclusive, ctx.bars());
    for (int t = from; t + horizon < end; t += horizon) {
        double ic = signal_evaluator::spearman(factor.scores(ctx, t),
                                               signal_evaluator::forwardReturns(ctx, t, horizon));
        if (!std::isnan(ic)) {
            sum += ic;
            n++;
        }
    }
    return n == 0 ? kNaN : sum / n;
}

WalkForwardResult walkForward(const AlphaContext& ctx, const std::vector<AlphaFactor>& candidates,
                              int horizon, int startIndex, int trainBars, int testBars) {
    if (candidates.empty() || trainBars <= horizon || testBars <= horizon) {
        throw std::invalid_argument(
                "need candidates and train/test windows longer than the horizon");
    }
    std::vector<int> dates = grid(ctx, startIndex, horizon);
    std::vector<std::vector<double>> ic = icMatrix(ctx, candidates, dates, horizon);

    WalkForwardResult result;
    double isSum = 0;
    double oosSum = 0;
    for (int trainStart = startIndex; trainStart + trainBars + testBars <= ctx.bars();
         trainStart += testBars) {
        int testStart = trainStart + trainBars;
        int testEnd = testStart + testBars;
        int bestIdx = -1;
        double bestIc = -std::numeric_limits<double>::infinity();
        for (std::size_t c = 0; c < candidates.size(); c++) {
            double mean = windowMean(ic[c], dates, trainStart, testStart, horizon);
            if (!std::isnan(mean) && mean > bestIc) {
                bestIc = mean;
                bestIdx = static_cast<int>(c);
            }
        }
        if (bestIdx < 0) {
            throw std::invalid_argument(
                    "no candidate produced a training IC in this fold -- factor warm-up likely "
                    "exceeds the training window");
        }
        double oos = windowMean(ic[static_cast<std::size_t>(bestIdx)], dates, testStart, testEnd,
                                horizon);
        Fold fold;
        fold.trainStart = trainStart;
        fold.testStart = testStart;
        fold.testEnd = testEnd;
        fold.chosenFactor = candidates[static_cast<std::size_t>(bestIdx)].name();
        fold.inSampleIc = bestIc;
        fold.outOfSampleIc = oos;
        result.folds.push_back(fold);
        isSum += bestIc;
        oosSum += oos;
    }
    if (result.folds.empty()) {
        throw std::invalid_argument("sample too short for one train+test fold");
    }
    result.meanInSampleIc = isSum / result.folds.size();
    result.meanOutOfSampleIc = oosSum / result.folds.size();
    return result;
}

double CrossValidationResult::signConsistency() const {
    if (blockIcs.empty()) {
        return 0;
    }
    double sign = meanIc > 0 ? 1.0 : (meanIc < 0 ? -1.0 : 0.0);
    int agree = 0;
    for (double ic : blockIcs) {
        double icSign = ic > 0 ? 1.0 : (ic < 0 ? -1.0 : 0.0);
        if (icSign == sign) {
            agree++;
        }
    }
    return static_cast<double>(agree) / static_cast<double>(blockIcs.size());
}

CrossValidationResult crossValidate(const AlphaContext& ctx, const AlphaFactor& factor, int horizon,
                                    int startIndex, int k) {
    int span = ctx.bars() - startIndex;
    if (k < 2 || span / k <= horizon) {
        throw std::invalid_argument("blocks must be longer than the horizon");
    }
    std::vector<double> ics(static_cast<std::size_t>(k));
    int blockLen = span / k;
    for (int b = 0; b < k; b++) {
        int from = startIndex + b * blockLen;
        int to = b == k - 1 ? ctx.bars() : from + blockLen;
        ics[static_cast<std::size_t>(b)] = meanIc(ctx, factor, from, to, horizon);
    }
    CrossValidationResult r;
    r.blockIcs = ics;
    r.meanIc = math_utils::mean(ics);
    r.icStd = math_utils::stdDev(ics);
    return r;
}

RobustnessResult monteCarloRobustness(const AlphaContext& ctx, const AlphaFactor& factor, int horizon,
                                      int startIndex, int trials, std::int64_t seed) {
    if (trials < 10) {
        throw std::invalid_argument("need at least 10 trials");
    }
    std::vector<std::vector<double>> scores;
    std::vector<std::vector<double>> forwards;
    for (int t = startIndex; t + horizon < ctx.bars(); t += horizon) {
        scores.push_back(factor.scores(ctx, t));
        forwards.push_back(signal_evaluator::forwardReturns(ctx, t, horizon));
    }
    int dates = static_cast<int>(scores.size());
    if (dates < 3) {
        throw std::invalid_argument("too few evaluation dates for a permutation test");
    }
    double observed = meanIcOf(scores, forwards, identity(dates));

    alpha_detail::JavaRandomInt rng(seed);
    double nullSum = 0;
    double nullSq = 0;
    int asExtreme = 0;
    std::vector<int> perm = identity(dates);
    for (int trial = 0; trial < trials; trial++) {
        shuffle(perm, rng);
        double icNull = meanIcOf(scores, forwards, perm);
        nullSum += icNull;
        nullSq += icNull * icNull;
        if (std::abs(icNull) >= std::abs(observed)) {
            asExtreme++;
        }
    }
    double nullMean = nullSum / trials;
    double nullVar = nullSq / trials - nullMean * nullMean;
    double p = (asExtreme + 1.0) / (trials + 1.0);
    RobustnessResult r;
    r.observedMeanIc = observed;
    r.pValue = p;
    r.nullMean = nullMean;
    r.nullStd = std::sqrt(std::max(0.0, nullVar));
    r.trials = trials;
    return r;
}

std::string SensitivityResult::best() const {
    std::size_t bestIdx = 0;
    for (std::size_t i = 1; i < meanIcs.size(); i++) {
        if (meanIcs[i] > meanIcs[bestIdx]) {
            bestIdx = i;
        }
    }
    return names[bestIdx];
}

SensitivityResult parameterSensitivity(const AlphaContext& ctx, const std::vector<AlphaFactor>& sweep,
                                       int horizon, int startIndex) {
    if (sweep.size() < 2) {
        throw std::invalid_argument("a sweep needs at least 2 candidates");
    }
    std::vector<int> dates = grid(ctx, startIndex, horizon);
    std::vector<std::vector<double>> icByCandidate = icMatrix(ctx, sweep, dates, horizon);
    SensitivityResult r;
    r.names.resize(sweep.size());
    r.meanIcs.resize(sweep.size());
    for (std::size_t i = 0; i < sweep.size(); i++) {
        r.names[i] = sweep[i].name();
        r.meanIcs[i] = windowMean(icByCandidate[i], dates, startIndex, ctx.bars(), horizon);
    }
    r.worstNeighborDrop = 0;
    for (std::size_t i = 1; i < r.meanIcs.size(); i++) {
        r.worstNeighborDrop = std::max(r.worstNeighborDrop, std::abs(r.meanIcs[i] - r.meanIcs[i - 1]));
    }
    return r;
}

}  // namespace alpha_validation
}  // namespace quantfinlib
