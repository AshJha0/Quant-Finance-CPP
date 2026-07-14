#include "quantfinlib/credit/cva_approximator.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace cva_approximator {

double cva(const std::vector<double>& expectedExposure,
           const std::vector<double>& bucketEndYears,
           const CreditCurve& counterparty, const YieldCurve& discount, double lgd) {
    std::size_t n = expectedExposure.size();
    if (n == 0 || bucketEndYears.size() != n) {
        throw std::invalid_argument("need aligned, non-empty exposure/time arrays, got "
                + std::to_string(n) + "/" + std::to_string(bucketEndYears.size()));
    }
    if (!(lgd > 0) || !(lgd <= 1)) {
        throw std::invalid_argument("lgd must be in (0, 1], got " + std::to_string(lgd));
    }
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double prevT = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (!(bucketEndYears[i] > prevT) || bucketEndYears[i] == kInf) {
            throw std::invalid_argument(
                    "bucket times must be strictly ascending, positive and finite: t["
                    + std::to_string(i) + "]=" + std::to_string(bucketEndYears[i]));
        }
        prevT = bucketEndYears[i];
        if (!(expectedExposure[i] >= 0) || expectedExposure[i] == kInf) {
            throw std::invalid_argument("expected exposure must be >= 0 and finite: EE["
                    + std::to_string(i) + "]=" + std::to_string(expectedExposure[i]));
        }
    }
    double sum = 0;
    double prevQ = 1;  // Q(0)
    for (std::size_t i = 0; i < n; i++) {
        double q = counterparty.survivalProbability(bucketEndYears[i]);
        sum += expectedExposure[i] * (prevQ - q)
                * discount.discountFactor(bucketEndYears[i]);
        prevQ = q;
    }
    return lgd * sum;
}

}  // namespace cva_approximator
}  // namespace quantfinlib
