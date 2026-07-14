#include "quantfinlib/backtest/portfolio/constrained_portfolio_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "quantfinlib/backtest/validation/splittable_random.hpp"

namespace quantfinlib {

ConstrainedPortfolioOptimizer::ConstrainedPortfolioOptimizer(
        std::vector<double> expectedReturns, math_utils::Matrix covariance,
        std::int64_t seed)
        : expectedReturns_(std::move(expectedReturns)),
          covariance_(std::move(covariance)),
          seed_(seed),
          minWeights_(expectedReturns_.size(), 0.0),
          maxWeights_(expectedReturns_.size(), 1.0) {}

ConstrainedPortfolioOptimizer& ConstrainedPortfolioOptimizer::withBounds(
        const std::vector<double>& min, const std::vector<double>& max) {
    double minSum = 0, maxSum = 0;
    for (std::size_t i = 0; i < min.size(); i++) {
        if (min[i] < 0 || max[i] > 1 || min[i] > max[i]) {
            throw std::invalid_argument("bad bounds for asset " + std::to_string(i));
        }
        minSum += min[i];
        maxSum += max[i];
    }
    if (minSum > 1 + 1e-12 || maxSum < 1 - 1e-12) {
        throw std::invalid_argument("bounds admit no fully-invested portfolio");
    }
    minWeights_ = min;
    maxWeights_ = max;
    return *this;
}

ConstrainedPortfolioOptimizer& ConstrainedPortfolioOptimizer::withTurnoverPenalty(
        const std::vector<double>& currentWeights, double penaltyPerUnitTurnover) {
    currentWeights_ = currentWeights;
    turnoverPenalty_ = penaltyPerUnitTurnover;
    return *this;
}

Allocation ConstrainedPortfolioOptimizer::maxSharpe(double riskFreeRate) const {
    std::vector<double> w = optimize([&](const std::vector<double>& x) {
        double vol = std::sqrt(std::max(0.0, math_utils::quadraticForm(x, covariance_)));
        return vol == 0 ? -std::numeric_limits<double>::infinity()
                        : (adjustedReturn(x) - riskFreeRate) / vol;
    });
    return toAllocation(std::move(w), riskFreeRate);
}

Allocation ConstrainedPortfolioOptimizer::minVolatility() const {
    std::vector<double> w = optimize([&](const std::vector<double>& x) {
        return -(std::sqrt(std::max(0.0, math_utils::quadraticForm(x, covariance_)))
                 + turnoverCost(x));
    });
    return toAllocation(std::move(w), 0);
}

// ---------------------------------------------------------------------

double ConstrainedPortfolioOptimizer::adjustedReturn(const std::vector<double>& w) const {
    return math_utils::dot(expectedReturns_, w) - turnoverCost(w);
}

double ConstrainedPortfolioOptimizer::turnoverCost(const std::vector<double>& w) const {
    if (currentWeights_.empty() || turnoverPenalty_ == 0) {
        return 0;
    }
    double turnover = 0;
    for (std::size_t i = 0; i < w.size(); i++) {
        turnover += std::fabs(w[i] - currentWeights_[i]);
    }
    return turnoverPenalty_ * turnover;
}

std::vector<double> ConstrainedPortfolioOptimizer::optimize(
        const std::function<double(const std::vector<double>&)>& objective) const {
    std::size_t n = expectedReturns_.size();
    SplittableRandom rnd(seed_);

    std::vector<double> best = project(std::vector<double>(n, 1.0 / static_cast<double>(n)));
    double bestScore = objective(best);
    std::vector<double> cand(n);
    for (int s = 0; s < 20000; s++) {
        double sum = 0;
        for (std::size_t i = 0; i < n; i++) {
            cand[i] = -std::log(rnd.nextDouble());
            sum += cand[i];
        }
        for (std::size_t i = 0; i < n; i++) {
            cand[i] /= sum;
        }
        std::vector<double> feasible = project(cand);
        double score = objective(feasible);
        if (score > bestScore) {
            bestScore = score;
            best = std::move(feasible);
        }
    }
    // Pairwise-transfer refinement within bounds.
    double step = 0.05;
    std::vector<double> trial(n);
    for (int sweep = 0; sweep < 80; sweep++) {
        bool improved = false;
        for (std::size_t from = 0; from < n; from++) {
            for (std::size_t to = 0; to < n; to++) {
                if (to == from) {
                    continue;
                }
                double move = std::min(step,
                        std::min(best[from] - minWeights_[from], maxWeights_[to] - best[to]));
                if (move <= 0) {
                    continue;
                }
                trial = best;
                trial[from] -= move;
                trial[to] += move;
                double score = objective(trial);
                if (score > bestScore) {
                    bestScore = score;
                    best = trial;
                    improved = true;
                }
            }
        }
        if (!improved) {
            step /= 2;
            if (step < 1e-6) {
                break;
            }
        }
    }
    return best;
}

/** Projects onto the box-constrained simplex (clip, then redistribute). */
std::vector<double> ConstrainedPortfolioOptimizer::project(std::vector<double> w) const {
    std::size_t n = w.size();
    for (int pass = 0; pass < 100; pass++) {
        double sum = 0;
        for (std::size_t i = 0; i < n; i++) {
            w[i] = math_utils::clamp(w[i], minWeights_[i], maxWeights_[i]);
            sum += w[i];
        }
        double deficit = 1 - sum;
        if (std::fabs(deficit) < 1e-12) {
            return w;
        }
        // Distribute the deficit proportionally to the available headroom.
        double capacity = 0;
        for (std::size_t i = 0; i < n; i++) {
            capacity += deficit > 0 ? maxWeights_[i] - w[i] : w[i] - minWeights_[i];
        }
        if (capacity <= 0) {
            return w;  // bounds validated at configuration; defensive
        }
        for (std::size_t i = 0; i < n; i++) {
            double room = deficit > 0 ? maxWeights_[i] - w[i] : w[i] - minWeights_[i];
            w[i] += deficit * room / capacity;
        }
    }
    return w;
}

Allocation ConstrainedPortfolioOptimizer::toAllocation(std::vector<double> w,
                                                       double riskFreeRate) const {
    double ret = math_utils::dot(expectedReturns_, w);
    double vol = std::sqrt(std::max(0.0, math_utils::quadraticForm(w, covariance_)));
    Allocation a;
    a.weights = std::move(w);
    a.expectedReturn = ret;
    a.volatility = vol;
    a.sharpe = vol == 0 ? 0 : (ret - riskFreeRate) / vol;
    return a;
}

}  // namespace quantfinlib
