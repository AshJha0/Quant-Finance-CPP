#include "quantfinlib/alpha/portfolio_construction.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>

#include "quantfinlib/risk/risk_metrics.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace portfolio_construction {

const std::string UNKNOWN_SECTOR_PREFIX = "\x01UNKNOWN:";

std::vector<double> zScoreWeights(const std::vector<double>& scores, double grossTarget) {
    return zScoreWeights(scores, grossTarget, std::numeric_limits<double>::max());
}

std::vector<double> zScoreWeights(const std::vector<double>& scores, double grossTarget,
                                  double maxWeight) {
    if (grossTarget <= 0 || maxWeight <= 0) {
        throw std::invalid_argument("grossTarget and maxWeight must be > 0");
    }
    std::size_t n = scores.size();
    double mean = 0;
    int scored = 0;
    for (double s : scores) {
        if (!std::isnan(s)) {
            mean += s;
            scored++;
        }
    }
    if (scored < 2) {
        return std::vector<double>(n, 0.0);
    }
    mean /= scored;
    double var = 0;
    for (double s : scores) {
        if (!std::isnan(s)) {
            var += (s - mean) * (s - mean);
        }
    }
    double std_ = std::sqrt(var / scored);
    std::vector<double> w(n, 0.0);
    if (std_ == 0) {
        return w;
    }
    for (std::size_t i = 0; i < n; i++) {
        if (!std::isnan(scores[i])) {
            w[i] = std::max(-3.0, std::min(3.0, (scores[i] - mean) / std_));
        }
    }
    normalizeGross(w, grossTarget);
    return capWeights(w, maxWeight, grossTarget);
}

std::vector<double> capWeights(const std::vector<double>& weights, double maxWeight,
                                double grossTarget) {
    std::vector<double> w = weights;
    bool capped = false;
    for (double& x : w) {
        if (std::abs(x) > maxWeight) {
            x = (x > 0 ? 1.0 : (x < 0 ? -1.0 : 0.0)) * maxWeight;
            capped = true;
        }
    }
    if (!capped) {
        return w;
    }
    double g = gross(w);
    if (g == 0) {
        return w;
    }
    double scale = grossTarget / g;
    for (double& x : w) {
        x = std::max(-maxWeight, std::min(maxWeight, x * scale));
    }
    return w;
}

std::vector<double> inverseVolBudget(const std::vector<double>& weights,
                                      const std::vector<double>& vols, double grossTarget) {
    if (weights.size() != vols.size()) {
        throw std::invalid_argument("weights and vols must align");
    }
    std::vector<double> w(weights.size(), 0.0);
    for (std::size_t i = 0; i < w.size(); i++) {
        if (weights[i] != 0) {
            if (std::isnan(vols[i]) || vols[i] <= 0) {
                throw std::invalid_argument("position has no usable vol");
            }
            w[i] = weights[i] / vols[i];
        }
    }
    normalizeGross(w, grossTarget);
    return w;
}

std::vector<double> trailingVols(const AlphaContext& ctx, int index, int lookback) {
    if (index < lookback) {
        throw std::invalid_argument("index < lookback");
    }
    std::vector<double> vols(static_cast<std::size_t>(ctx.symbolCount()));
    for (int i = 0; i < ctx.symbolCount(); i++) {
        std::vector<double> r(static_cast<std::size_t>(lookback));
        for (int j = 0; j < lookback; j++) {
            r[static_cast<std::size_t>(j)] =
                    ctx.returnOver(i, index - lookback + j, index - lookback + j + 1);
        }
        vols[static_cast<std::size_t>(i)] = math_utils::stdDev(r);
    }
    return vols;
}

std::vector<double> sectorNeutralize(const AlphaContext& ctx, const std::vector<double>& weights,
                                     const std::unordered_map<std::string, std::string>& sectorBySymbol) {
    if (static_cast<int>(weights.size()) != ctx.symbolCount()) {
        throw std::invalid_argument("weights must align with the context panel");
    }
    std::vector<std::string> sectors(weights.size());
    for (std::size_t i = 0; i < sectors.size(); i++) {
        const std::string& sym = ctx.symbols()[i];
        auto it = sectorBySymbol.find(sym);
        sectors[i] = it != sectorBySymbol.end() ? it->second : (UNKNOWN_SECTOR_PREFIX + sym);
    }
    return sectorNeutralize(weights, sectors);
}

std::vector<double> sectorNeutralize(const std::vector<double>& weights,
                                     const std::vector<std::string>& sectors) {
    if (weights.size() != sectors.size()) {
        throw std::invalid_argument("weights and sectors must align");
    }
    std::vector<double> w = weights;
    std::unordered_map<std::string, std::pair<double, double>> sums;  // sum, count
    for (std::size_t i = 0; i < w.size(); i++) {
        if (w[i] != 0) {
            auto& s = sums[sectors[i]];
            s.first += w[i];
            s.second++;
        }
    }
    for (std::size_t i = 0; i < w.size(); i++) {
        if (w[i] != 0) {
            const auto& s = sums[sectors[i]];
            w[i] -= s.first / s.second;
        }
    }
    return w;
}

std::vector<double> betaNeutralize(const std::vector<double>& weights, const std::vector<double>& betas) {
    if (weights.size() != betas.size()) {
        throw std::invalid_argument("weights and betas must align");
    }
    double wb = 0;
    double bb = 0;
    for (std::size_t i = 0; i < weights.size(); i++) {
        if (weights[i] != 0) {
            if (std::isnan(betas[i])) {
                throw std::invalid_argument("position has NaN beta");
            }
            wb += weights[i] * betas[i];
            bb += betas[i] * betas[i];
        }
    }
    std::vector<double> w = weights;
    if (bb == 0) {
        return w;
    }
    double lambda = wb / bb;
    for (std::size_t i = 0; i < w.size(); i++) {
        if (w[i] != 0) {
            w[i] -= lambda * betas[i];
        }
    }
    return w;
}

std::vector<double> trailingBetas(const AlphaContext& ctx, int index, int lookback) {
    if (index < lookback) {
        throw std::invalid_argument("index < lookback");
    }
    int n = ctx.symbolCount();
    std::vector<std::vector<double>> returns(static_cast<std::size_t>(n),
                                             std::vector<double>(static_cast<std::size_t>(lookback)));
    std::vector<double> market(static_cast<std::size_t>(lookback));
    for (int j = 0; j < lookback; j++) {
        double sum = 0;
        for (int i = 0; i < n; i++) {
            double r = ctx.returnOver(i, index - lookback + j, index - lookback + j + 1);
            returns[static_cast<std::size_t>(i)][static_cast<std::size_t>(j)] = r;
            sum += r;
        }
        market[static_cast<std::size_t>(j)] = sum / n;
    }
    std::vector<double> betas(static_cast<std::size_t>(n));
    for (int i = 0; i < n; i++) {
        betas[static_cast<std::size_t>(i)] =
                risk_metrics::beta(returns[static_cast<std::size_t>(i)], market);
    }
    return betas;
}

std::vector<double> meanVarianceTilt(const std::vector<double>& alphas,
                                     const std::vector<std::vector<double>>& covariance,
                                     double grossTarget) {
    std::size_t n = alphas.size();
    if (covariance.size() != n) {
        throw std::invalid_argument("alphas and covariance must align");
    }
    std::size_t m = 0;
    std::vector<std::size_t> map(n);
    for (std::size_t i = 0; i < n; i++) {
        if (!std::isnan(alphas[i])) {
            map[m++] = i;
        }
    }
    if (m == 0) {
        return std::vector<double>(n, 0.0);
    }
    std::vector<std::vector<double>> cov(m, std::vector<double>(m));
    std::vector<double> a(m);
    for (std::size_t i = 0; i < m; i++) {
        a[i] = alphas[map[i]];
        for (std::size_t j = 0; j < m; j++) {
            cov[i][j] = covariance[map[i]][map[j]];
        }
    }
    std::vector<double> solved;
    try {
        solved = math_utils::solveLinear(cov, a);
    } catch (const std::invalid_argument& e) {
        throw std::invalid_argument(
                std::string("covariance is singular (a flat/duplicated/forward-filled series has "
                            "zero or dependent variance) -- shrink or regularize it, e.g. add "
                            "lambda*I (")
                + e.what() + ")");
    }
    std::vector<double> w(n, 0.0);
    for (std::size_t i = 0; i < m; i++) {
        w[map[i]] = solved[i];
    }
    normalizeGross(w, grossTarget);
    return w;
}

void normalizeGross(std::vector<double>& w, double grossTarget) {
    double g = gross(w);
    if (g == 0) {
        return;
    }
    double scale = grossTarget / g;
    for (double& x : w) {
        x *= scale;
    }
}

double gross(const std::vector<double>& w) {
    double g = 0;
    for (double x : w) {
        g += std::abs(x);
    }
    return g;
}

}  // namespace portfolio_construction
}  // namespace quantfinlib
