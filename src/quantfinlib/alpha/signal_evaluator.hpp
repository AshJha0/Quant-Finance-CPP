// Port of com.quantfinlib.alpha.SignalEvaluator: signal evaluation -- rank
// IC (Spearman), IR, t-stat, hit rate, turnover, quantile returns and
// factor exposure. Evaluation dates step by the horizon so forward-return
// windows don't overlap.
#pragma once

#include <string>
#include <vector>

#include "quantfinlib/alpha/alpha_context.hpp"
#include "quantfinlib/alpha/alpha_factor.hpp"

namespace quantfinlib {
namespace signal_evaluator {

/** The evaluation scorecard. */
struct Report {
    std::string factorName;
    double meanIc = 0;
    double icStd = 0;
    double ir = 0;
    double tStat = 0;
    double hitRate = 0;
    double meanTurnover = 0;
    int observations = 0;
    std::vector<double> icSeries;

    /** One-line human summary in fixed order for report diffs. */
    std::string format() const;
};

/** Mean forward return per score quantile. */
struct QuantileReport {
    std::string factorName;
    std::vector<double> meanReturns;
    std::vector<int> counts;
    int quantiles = 0;
    int dates = 0;

    /** Top-quantile minus bottom-quantile mean forward return. */
    double spread() const { return meanReturns.back() - meanReturns.front(); }
};

/** Evaluates a factor over [startIndex, ctx.bars() - horizon), stepping by horizon. */
Report evaluate(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex, int horizon);

/** Buckets each evaluation date's cross-section into `quantiles` score-ranked groups. */
QuantileReport quantileReturns(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex,
                                int horizon, int quantiles);

/** Mean cross-sectional rank correlation between two factors' scores. */
double factorExposure(const AlphaContext& ctx, const AlphaFactor& a, const AlphaFactor& b,
                       int startIndex, int step);

/** Forward simple returns over (t, t+horizon], aligned with symbols. */
std::vector<double> forwardReturns(const AlphaContext& ctx, int t, int horizon);

/**
 * Spearman rank correlation over pairwise-complete entries; NaN when fewer
 * than 3 complete pairs.
 */
double spearman(const std::vector<double>& x, const std::vector<double>& y);

}  // namespace signal_evaluator
}  // namespace quantfinlib
