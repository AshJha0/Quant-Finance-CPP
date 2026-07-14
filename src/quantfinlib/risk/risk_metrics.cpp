#include "quantfinlib/risk/risk_metrics.hpp"

#include <algorithm>
#include <cmath>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace risk_metrics {

double volatility(const std::vector<double>& returns) {
    return math_utils::stdDev(returns);
}

double annualizedVolatility(const std::vector<double>& returns, int periodsPerYear) {
    return volatility(returns) * std::sqrt(static_cast<double>(periodsPerYear));
}

double historicalVar(const std::vector<double>& returns, double confidence) {
    double q = math_utils::percentile(returns, 1 - confidence);
    return std::max(0.0, -q);
}

double parametricVar(const std::vector<double>& returns, double confidence) {
    double mu = math_utils::mean(returns);
    double sigma = math_utils::stdDev(returns);
    double z = math_utils::normInv(1 - confidence);
    return std::max(0.0, -(mu + z * sigma));
}

double conditionalVar(const std::vector<double>& returns, double confidence) {
    double threshold = math_utils::percentile(returns, 1 - confidence);
    double sum = 0;
    int count = 0;
    for (double r : returns) {
        if (r <= threshold) {
            sum += r;
            count++;
        }
    }
    return count == 0 ? 0 : std::max(0.0, -sum / count);
}

double expectedShortfall(const std::vector<double>& returns, double confidence) {
    return conditionalVar(returns, confidence);
}

double sharpeRatio(const std::vector<double>& returns, double riskFreeRate,
                   int periodsPerYear) {
    double vol = annualizedVolatility(returns, periodsPerYear);
    if (vol == 0) {
        return 0;
    }
    double annualReturn = math_utils::mean(returns) * periodsPerYear;
    return (annualReturn - riskFreeRate) / vol;
}

double sortinoRatio(const std::vector<double>& returns, double riskFreeRate,
                    int periodsPerYear) {
    double mar = riskFreeRate / periodsPerYear;
    double dd = downsideDeviation(returns, mar)
            * std::sqrt(static_cast<double>(periodsPerYear));
    if (dd == 0) {
        return 0;
    }
    double annualReturn = math_utils::mean(returns) * periodsPerYear;
    return (annualReturn - riskFreeRate) / dd;
}

double downsideDeviation(const std::vector<double>& returns, double mar) {
    double s = 0;
    for (double r : returns) {
        double d = std::min(0.0, r - mar);
        s += d * d;
    }
    return std::sqrt(s / static_cast<double>(returns.size()));
}

double maxDrawdown(const std::vector<double>& equity) {
    double peak = equity[0];
    double maxDd = 0;
    for (double e : equity) {
        peak = std::max(peak, e);
        if (peak > 0) {
            maxDd = std::max(maxDd, (peak - e) / peak);
        }
    }
    return maxDd;
}

double beta(const std::vector<double>& assetReturns,
            const std::vector<double>& benchmarkReturns) {
    double varB = math_utils::variance(benchmarkReturns);
    if (varB == 0) {
        return 0;
    }
    return math_utils::covariance(assetReturns, benchmarkReturns) / varB;
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    return math_utils::correlation(a, b);
}

}  // namespace risk_metrics
}  // namespace quantfinlib
