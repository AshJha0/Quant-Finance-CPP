#include "quantfinlib/alpha/fama_macbeth.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace fama_macbeth {

namespace {
bool hasNaN(const std::vector<double>& a) {
    for (double x : a) {
        if (std::isnan(x)) {
            return true;
        }
    }
    return false;
}

bool hasInfinity(const std::vector<double>& a) {
    for (double x : a) {
        if (std::isinf(x)) {
            return true;
        }
    }
    return false;
}
}  // namespace

Result fit(const std::vector<std::vector<std::vector<double>>>& exposures,
          const std::vector<std::vector<double>>& forwardReturns) {
    std::size_t periods = exposures.size();
    if (forwardReturns.size() != periods || periods < 12) {
        throw std::invalid_argument("need >= 12 aligned periods");
    }
    std::size_t factors = (!exposures[0].empty()) ? exposures[0][0].size() : 0;
    if (factors < 1) {
        throw std::invalid_argument("need >= 1 factor");
    }
    std::size_t dim = factors + 1;
    std::vector<std::vector<double>> lambdas;
    lambdas.reserve(periods);
    std::vector<std::vector<double>> xtx(dim, std::vector<double>(dim));
    std::vector<double> xty(dim);
    std::vector<double> row(dim);
    for (std::size_t t = 0; t < periods; t++) {
        std::size_t assets = exposures[t].size();
        if (forwardReturns[t].size() != assets) {
            throw std::invalid_argument("period misaligned");
        }
        for (auto& r : xtx) {
            std::fill(r.begin(), r.end(), 0.0);
        }
        std::fill(xty.begin(), xty.end(), 0.0);
        std::size_t rows = 0;
        for (std::size_t a = 0; a < assets; a++) {
            if (exposures[t][a].size() != factors) {
                throw std::invalid_argument("asset has wrong factor count");
            }
            double y = forwardReturns[t][a];
            if (std::isnan(y) || hasNaN(exposures[t][a])) {
                continue;
            }
            if (std::isinf(y) || hasInfinity(exposures[t][a])) {
                throw std::invalid_argument("infinite value -- a data error, not a missing name");
            }
            row[0] = 1;
            for (std::size_t k = 0; k < factors; k++) {
                row[k + 1] = exposures[t][a][k];
            }
            for (std::size_t i = 0; i < dim; i++) {
                for (std::size_t j = 0; j < dim; j++) {
                    xtx[i][j] += row[i] * row[j];
                }
                xty[i] += row[i] * y;
            }
            rows++;
        }
        if (rows < factors + 2) {
            continue;
        }
        try {
            lambdas.push_back(math_utils::solveLinear(xtx, xty));
        } catch (const std::invalid_argument&) {
            // A collinear cross-section prices nothing that period. Skip it
            // like a thin period.
        }
    }
    std::size_t used = lambdas.size();
    if (used < 12) {
        throw std::invalid_argument("too few usable cross-sections -- premia need a time series");
    }
    Result result;
    result.premia.assign(factors, 0.0);
    result.tStats.assign(factors, 0.0);
    for (std::size_t k = 0; k <= factors; k++) {
        std::vector<double> series(used);
        for (std::size_t t = 0; t < used; t++) {
            series[t] = lambdas[t][k];
        }
        double mean = math_utils::mean(series);
        double se = math_utils::stdDev(series) / std::sqrt(static_cast<double>(used));
        double tStat = se > 0 ? mean / se
                              : (mean > 0 ? std::numeric_limits<double>::infinity()
                                          : (mean < 0 ? -std::numeric_limits<double>::infinity() : 0.0));
        if (k == 0) {
            result.interceptMean = mean;
            result.interceptTStat = tStat;
        } else {
            result.premia[k - 1] = mean;
            result.tStats[k - 1] = tStat;
        }
    }
    result.periodsUsed = static_cast<int>(used);
    return result;
}

}  // namespace fama_macbeth
}  // namespace quantfinlib
