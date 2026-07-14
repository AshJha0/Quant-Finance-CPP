#include "quantfinlib/execution/futures_roll_algo.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

FuturesRollAlgo::FuturesRollAlgo(std::int64_t positionContracts, const std::vector<double>& cumulativeMigration)
        : position_(positionContracts), cumulativeCurve_(cumulativeMigration) {
    if (positionContracts <= 0) {
        throw std::invalid_argument("positionContracts must be > 0");
    }
    if (cumulativeMigration.empty()) {
        throw std::invalid_argument("need at least one roll day");
    }
    double prev = 0;
    for (double c : cumulativeMigration) {
        if (!(c >= prev) || c > 1) {
            throw std::invalid_argument("migration curve must be non-decreasing within [0, 1]");
        }
        prev = c;
    }
    if (cumulativeMigration.back() != 1.0) {
        throw std::invalid_argument(
                "the curve must END at exactly 1 -- a roll that does not complete "
                "is a delivery notice waiting to happen");
    }
}

std::vector<double> FuturesRollAlgo::defaultMigration(int days) {
    if (days < 1) {
        throw std::invalid_argument("need >= 1 roll day");
    }
    std::vector<double> curve(static_cast<std::size_t>(days), 0.0);
    for (int d = 0; d < days; d++) {
        double x = (d + 1) / static_cast<double>(days);
        curve[static_cast<std::size_t>(d)] = x * x * (3 - 2 * x);
    }
    curve[static_cast<std::size_t>(days - 1)] = 1.0;  // exact, not 0.999999
    return curve;
}

std::int64_t FuturesRollAlgo::dueOnDay(int day) const {
    if (day < 0 || day >= static_cast<int>(cumulativeCurve_.size())) {
        throw std::invalid_argument("day " + std::to_string(day) + " outside the roll window");
    }
    auto target = static_cast<std::int64_t>(
            std::llround(static_cast<double>(position_) * cumulativeCurve_[static_cast<std::size_t>(day)]));
    return std::max<std::int64_t>(0, target - rolled_);
}

void FuturesRollAlgo::onRolled(std::int64_t contracts) {
    if (contracts < 0 || rolled_ + contracts > position_) {
        throw std::invalid_argument("rolled " + std::to_string(contracts) + " would exceed the position");
    }
    rolled_ += contracts;
}

}  // namespace quantfinlib
