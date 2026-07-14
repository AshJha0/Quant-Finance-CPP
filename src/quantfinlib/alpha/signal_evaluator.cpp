#include "quantfinlib/alpha/signal_evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <numeric>
#include <stdexcept>

#include "quantfinlib/alpha/portfolio_construction.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace signal_evaluator {

namespace {

/** First index with sorted[idx] >= key. */
int lowerBound(const std::vector<double>& sorted, double key) {
    int lo = 0;
    int hi = static_cast<int>(sorted.size());
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sorted[static_cast<std::size_t>(mid)] < key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/** First index with sorted[idx] > key. */
int upperBound(const std::vector<double>& sorted, double key) {
    int lo = 0;
    int hi = static_cast<int>(sorted.size());
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (sorted[static_cast<std::size_t>(mid)] <= key) {
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    return lo;
}

/** Midrank transform (average rank for ties), 1-based -- values only feed Pearson. */
std::vector<double> ranks(const std::vector<double>& v) {
    std::size_t n = v.size();
    std::vector<double> sorted = v;
    std::sort(sorted.begin(), sorted.end());
    std::vector<double> rank(n);
    for (std::size_t i = 0; i < n; i++) {
        int lo = lowerBound(sorted, v[i]);
        int hi = upperBound(sorted, v[i]);
        rank[i] = (lo + hi - 1) / 2.0 + 1;
    }
    return rank;
}

}  // namespace

std::string Report::format() const {
    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s: IC=%.4f (t=%.2f, n=%d) IR=%.2f hit=%.1f%% turnover=%.1f%%",
                  factorName.c_str(), meanIc, tStat, observations, ir, hitRate * 100,
                  meanTurnover * 100);
    return std::string(buf);
}

std::vector<double> forwardReturns(const AlphaContext& ctx, int t, int horizon) {
    std::vector<double> fwd(static_cast<std::size_t>(ctx.symbolCount()));
    for (int i = 0; i < ctx.symbolCount(); i++) {
        fwd[static_cast<std::size_t>(i)] = ctx.returnOver(i, t, t + horizon);
    }
    return fwd;
}

double spearman(const std::vector<double>& x, const std::vector<double>& y) {
    int n = 0;
    for (std::size_t i = 0; i < x.size(); i++) {
        if (!std::isnan(x[i]) && !std::isnan(y[i])) {
            n++;
        }
    }
    if (n < 3) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    std::vector<double> xs(static_cast<std::size_t>(n));
    std::vector<double> ys(static_cast<std::size_t>(n));
    int k = 0;
    for (std::size_t i = 0; i < x.size(); i++) {
        if (!std::isnan(x[i]) && !std::isnan(y[i])) {
            xs[static_cast<std::size_t>(k)] = x[i];
            ys[static_cast<std::size_t>(k)] = y[i];
            k++;
        }
    }
    return math_utils::correlation(ranks(xs), ranks(ys));
}

Report evaluate(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex, int horizon) {
    if (horizon <= 0 || startIndex < 0) {
        throw std::invalid_argument("need horizon > 0 and startIndex >= 0");
    }
    std::vector<double> ics;
    double hits = 0;
    double hitPairs = 0;
    double turnoverSum = 0;
    int turnoverCount = 0;
    std::vector<double> prevWeights;
    bool hasPrev = false;

    for (int t = startIndex; t + horizon < ctx.bars(); t += horizon) {
        std::vector<double> scores = factor.scores(ctx, t);
        std::vector<double> fwd = forwardReturns(ctx, t, horizon);

        double ic = spearman(scores, fwd);
        if (std::isnan(ic)) {
            continue;
        }
        ics.push_back(ic);
        for (std::size_t i = 0; i < scores.size(); i++) {
            if (!std::isnan(scores[i]) && scores[i] != 0 && !std::isnan(fwd[i])) {
                hitPairs++;
                double sSign = scores[i] > 0 ? 1.0 : (scores[i] < 0 ? -1.0 : 0.0);
                double fSign = fwd[i] > 0 ? 1.0 : (fwd[i] < 0 ? -1.0 : 0.0);
                if (sSign == fSign) {
                    hits++;
                }
            }
        }
        std::vector<double> w = portfolio_construction::zScoreWeights(scores, 1.0);
        if (hasPrev) {
            double l1 = 0;
            for (std::size_t i = 0; i < w.size(); i++) {
                l1 += std::abs(w[i] - prevWeights[i]);
            }
            turnoverSum += l1 / 2;
            turnoverCount++;
        }
        prevWeights = w;
        hasPrev = true;
    }

    int n = static_cast<int>(ics.size());
    if (n < 2) {
        throw std::invalid_argument(
                "fewer than 2 IC observations -- extend the sample or shrink the horizon");
    }
    double mean = math_utils::mean(ics);
    double std_ = math_utils::stdDev(ics);
    double ir = std_ == 0 ? 0 : mean / std_;
    Report r;
    r.factorName = factor.name();
    r.meanIc = mean;
    r.icStd = std_;
    r.ir = ir;
    r.tStat = std_ == 0 ? 0 : mean / (std_ / std::sqrt(static_cast<double>(n)));
    r.hitRate = hitPairs == 0 ? 0 : hits / hitPairs;
    r.meanTurnover = turnoverCount == 0 ? 0 : turnoverSum / turnoverCount;
    r.observations = n;
    r.icSeries = ics;
    return r;
}

QuantileReport quantileReturns(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex,
                               int horizon, int quantiles) {
    if (horizon <= 0 || startIndex < 0) {
        throw std::invalid_argument("need horizon > 0 and startIndex >= 0");
    }
    if (quantiles < 2) {
        throw std::invalid_argument("need quantiles >= 2");
    }
    std::vector<double> sums(static_cast<std::size_t>(quantiles), 0.0);
    std::vector<int> counts(static_cast<std::size_t>(quantiles), 0);
    int dates = 0;
    for (int t = startIndex; t + horizon < ctx.bars(); t += horizon) {
        std::vector<double> scores = factor.scores(ctx, t);
        std::vector<double> fwd = forwardReturns(ctx, t, horizon);
        int n = 0;
        for (std::size_t i = 0; i < scores.size(); i++) {
            if (!std::isnan(scores[i]) && !std::isnan(fwd[i])) {
                n++;
            }
        }
        if (n < quantiles) {
            continue;
        }
        std::vector<double> s(static_cast<std::size_t>(n));
        std::vector<double> f(static_cast<std::size_t>(n));
        int k = 0;
        for (std::size_t i = 0; i < scores.size(); i++) {
            if (!std::isnan(scores[i]) && !std::isnan(fwd[i])) {
                s[static_cast<std::size_t>(k)] = scores[i];
                f[static_cast<std::size_t>(k)] = fwd[i];
                k++;
            }
        }
        std::vector<int> order(static_cast<std::size_t>(n));
        std::iota(order.begin(), order.end(), 0);
        std::sort(order.begin(), order.end(),
                  [&s](int a, int b) { return s[static_cast<std::size_t>(a)] < s[static_cast<std::size_t>(b)]; });
        for (int rank = 0; rank < n; rank++) {
            int bucket = static_cast<int>(static_cast<long long>(rank) * quantiles / n);
            sums[static_cast<std::size_t>(bucket)] += f[static_cast<std::size_t>(order[static_cast<std::size_t>(rank)])];
            counts[static_cast<std::size_t>(bucket)]++;
        }
        dates++;
    }
    if (dates == 0) {
        throw std::invalid_argument("no date had >= quantiles scored names");
    }
    std::vector<double> means(static_cast<std::size_t>(quantiles));
    for (int q = 0; q < quantiles; q++) {
        means[static_cast<std::size_t>(q)] =
                sums[static_cast<std::size_t>(q)] / counts[static_cast<std::size_t>(q)];
    }
    QuantileReport r;
    r.factorName = factor.name();
    r.meanReturns = means;
    r.counts = counts;
    r.quantiles = quantiles;
    r.dates = dates;
    return r;
}

double factorExposure(const AlphaContext& ctx, const AlphaFactor& a, const AlphaFactor& b,
                      int startIndex, int step) {
    double sum = 0;
    int n = 0;
    for (int t = startIndex; t < ctx.bars(); t += step) {
        double rho = spearman(a.scores(ctx, t), b.scores(ctx, t));
        if (!std::isnan(rho)) {
            sum += rho;
            n++;
        }
    }
    if (n == 0) {
        throw std::invalid_argument("no overlapping scored dates");
    }
    return sum / n;
}

}  // namespace signal_evaluator
}  // namespace quantfinlib
