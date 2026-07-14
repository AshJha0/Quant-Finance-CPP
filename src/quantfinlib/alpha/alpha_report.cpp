#include "quantfinlib/alpha/alpha_report.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <stdexcept>

#include "quantfinlib/alpha/alpha_validation.hpp"
#include "quantfinlib/backtest/performance_analytics.hpp"
#include "quantfinlib/backtest/trade.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace alpha_report {

std::string Decay::format() const {
    std::string sb = "alpha decay:";
    char buf[64];
    for (std::size_t i = 0; i < horizons.size(); i++) {
        std::snprintf(buf, sizeof(buf), " h=%d:%.4f", horizons[i], meanIcs[i]);
        sb += buf;
    }
    std::snprintf(buf, sizeof(buf), " | half-life=%.1f bars", halfLifeBars);
    sb += buf;
    return sb;
}

Decay decayProfile(const AlphaContext& ctx, const AlphaFactor& factor, int startIndex,
                   const std::vector<int>& horizons) {
    if (horizons.size() < 2) {
        throw std::invalid_argument("need at least 2 horizons");
    }
    std::vector<double> ics(horizons.size());
    for (std::size_t i = 0; i < horizons.size(); i++) {
        if (i > 0 && horizons[i] <= horizons[i - 1]) {
            throw std::invalid_argument("horizons must be ascending");
        }
        ics[i] = alpha_validation::meanIc(ctx, factor, startIndex, ctx.bars(), horizons[i]);
    }
    double target = ics[0] / 2;
    double halfLife = std::numeric_limits<double>::infinity();
    if (ics[0] > 0) {
        for (std::size_t i = 1; i < ics.size(); i++) {
            if (ics[i] < target) {
                double w = (ics[i - 1] - target) / (ics[i - 1] - ics[i]);
                halfLife = horizons[i - 1] + w * (horizons[i] - horizons[i - 1]);
                break;
            }
        }
    }
    Decay d;
    d.horizons = horizons;
    d.meanIcs = ics;
    d.halfLifeBars = halfLife;
    return d;
}

std::string Attribution::format() const {
    char buf[128];
    std::snprintf(buf, sizeof(buf), "attribution: alpha=%.6f/bar", alphaPerBar);
    std::string sb(buf);
    for (std::size_t i = 0; i < betas.size(); i++) {
        std::snprintf(buf, sizeof(buf), " %s=%.3f", factorNames[i].c_str(), betas[i]);
        sb += buf;
    }
    std::snprintf(buf, sizeof(buf), " R2=%.3f", rSquared);
    sb += buf;
    return sb;
}

Attribution attribute(const std::vector<double>& portfolioReturns,
                      const std::vector<std::vector<double>>& factorReturns,
                      const std::vector<std::string>& factorNames) {
    std::size_t n = portfolioReturns.size();
    std::size_t k = factorReturns.size();
    if (k != factorNames.size() || k == 0) {
        throw std::invalid_argument("factor streams and names must align");
    }
    for (const auto& f : factorReturns) {
        if (f.size() != n) {
            throw std::invalid_argument("factor stream length mismatch");
        }
    }
    if (n <= k + 1) {
        throw std::invalid_argument("more factors than observations");
    }
    for (std::size_t t = 0; t < n; t++) {
        if (std::isnan(portfolioReturns[t])) {
            throw std::invalid_argument("NaN portfolio return at index " + std::to_string(t));
        }
        for (std::size_t j = 0; j < k; j++) {
            if (std::isnan(factorReturns[j][t])) {
                throw std::invalid_argument("NaN in factor stream '" + factorNames[j] + "' at index "
                                            + std::to_string(t) + " -- trim warm-up bars before "
                                                                   "attribution");
            }
        }
    }
    std::size_t p = k + 1;
    std::vector<std::vector<double>> xtx(p, std::vector<double>(p, 0.0));
    std::vector<double> xty(p, 0.0);
    for (std::size_t t = 0; t < n; t++) {
        std::vector<double> row(p);
        row[0] = 1;
        for (std::size_t j = 0; j < k; j++) {
            row[j + 1] = factorReturns[j][t];
        }
        for (std::size_t a = 0; a < p; a++) {
            xty[a] += row[a] * portfolioReturns[t];
            for (std::size_t b = 0; b < p; b++) {
                xtx[a][b] += row[a] * row[b];
            }
        }
    }
    std::vector<double> beta = math_utils::solveLinear(xtx, xty);
    double meanY = math_utils::mean(portfolioReturns);
    double ssTot = 0;
    double ssRes = 0;
    for (std::size_t t = 0; t < n; t++) {
        double fit = beta[0];
        for (std::size_t j = 0; j < k; j++) {
            fit += beta[j + 1] * factorReturns[j][t];
        }
        ssRes += (portfolioReturns[t] - fit) * (portfolioReturns[t] - fit);
        ssTot += (portfolioReturns[t] - meanY) * (portfolioReturns[t] - meanY);
    }
    Attribution attr;
    attr.alphaPerBar = beta[0];
    attr.betas.assign(beta.begin() + 1, beta.end());
    attr.factorNames = factorNames;
    attr.rSquared = ssTot == 0 ? 0 : 1 - ssRes / ssTot;
    return attr;
}

std::vector<double> returnsOf(const std::vector<double>& equity) {
    if (equity.size() < 2) {
        throw std::invalid_argument("need at least 2 equity points");
    }
    std::vector<double> r(equity.size() - 1);
    for (std::size_t i = 0; i < r.size(); i++) {
        r[i] = equity[i + 1] / equity[i] - 1;
    }
    return r;
}

std::vector<double> drawdownCurve(const std::vector<double>& equity) {
    std::vector<double> dd(equity.size());
    double peak = equity[0];
    for (std::size_t i = 0; i < equity.size(); i++) {
        peak = std::max(peak, equity[i]);
        dd[i] = peak > 0 ? equity[i] / peak - 1 : 0;
    }
    return dd;
}

std::vector<double> rollingSharpe(const std::vector<double>& returns, int window, int periodsPerYear) {
    if (window < 2 || window > static_cast<int>(returns.size())) {
        throw std::invalid_argument("window must be in [2, returns.length]");
    }
    std::vector<double> out(returns.size());
    for (int i = 0; i < window - 1; i++) {
        out[static_cast<std::size_t>(i)] = std::numeric_limits<double>::quiet_NaN();
    }
    for (int i = window - 1; i < static_cast<int>(returns.size()); i++) {
        double mean = math_utils::mean(returns, i - window + 1, i + 1);
        double sd = math_utils::stdDevSample(returns, i - window + 1, i + 1);
        out[static_cast<std::size_t>(i)] = sd == 0 ? 0 : mean / sd * std::sqrt(static_cast<double>(periodsPerYear));
    }
    return out;
}

PerformanceMetrics summarize(const std::vector<double>& equity, int periodsPerYear) {
    return performance_analytics::compute(equity, {}, periodsPerYear);
}

}  // namespace alpha_report
}  // namespace quantfinlib
