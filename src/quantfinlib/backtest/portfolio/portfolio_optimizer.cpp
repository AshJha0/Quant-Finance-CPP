#include "quantfinlib/backtest/portfolio/portfolio_optimizer.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

#include "quantfinlib/backtest/validation/splittable_random.hpp"

namespace quantfinlib {

PortfolioOptimizer::PortfolioOptimizer(std::vector<double> expectedReturns,
                                       math_utils::Matrix covariance, std::int64_t seed)
        : expectedReturns_(std::move(expectedReturns)),
          covariance_(std::move(covariance)),
          seed_(seed) {
    if (expectedReturns_.size() != covariance_.size()) {
        throw std::invalid_argument("returns/covariance dimension mismatch");
    }
}

Allocation PortfolioOptimizer::maxSharpe(double riskFreeRate) const {
    std::vector<double> w = optimize([&](const std::vector<double>& x) {
        double vol = volatilityOf(x);
        return vol == 0 ? -std::numeric_limits<double>::infinity()
                        : (math_utils::dot(expectedReturns_, x) - riskFreeRate) / vol;
    });
    return toAllocation(std::move(w), riskFreeRate);
}

Allocation PortfolioOptimizer::minVolatility() const {
    std::vector<double> w = optimize(
            [&](const std::vector<double>& x) { return -volatilityOf(x); });
    return toAllocation(std::move(w), 0);
}

std::vector<Allocation> PortfolioOptimizer::efficientFrontier(int points) const {
    double lo = std::numeric_limits<double>::infinity();
    double hi = -std::numeric_limits<double>::infinity();
    for (double r : expectedReturns_) {
        lo = std::min(lo, r);
        hi = std::max(hi, r);
    }
    std::vector<Allocation> frontier;
    frontier.reserve(static_cast<std::size_t>(points));
    for (int p = 0; p < points; p++) {
        double target = lo + (hi - lo) * p / std::max(1, points - 1);
        // Penalized objective: minimize vol subject to hitting the target return.
        std::vector<double> w = optimize([&](const std::vector<double>& x) {
            double shortfall = target - math_utils::dot(expectedReturns_, x);
            double penalty = shortfall > 0 ? shortfall * 100 : 0;
            return -(volatilityOf(x) + penalty);
        });
        frontier.push_back(toAllocation(std::move(w), 0));
    }
    return frontier;
}

std::vector<double> PortfolioOptimizer::rebalance(const std::vector<double>& currentWeights,
                                                  const std::vector<double>& targetWeights) {
    std::vector<double> d(currentWeights.size());
    for (std::size_t i = 0; i < d.size(); i++) {
        d[i] = targetWeights[i] - currentWeights[i];
    }
    return d;
}

// ---------------------------------------------------------------------

std::vector<double> PortfolioOptimizer::optimize(
        const std::function<double(const std::vector<double>&)>& objective) const {
    std::size_t n = expectedReturns_.size();
    SplittableRandom rnd(seed_);

    // Phase 1: Dirichlet sampling over the simplex.
    std::vector<double> best(n, 1.0 / static_cast<double>(n));
    double bestScore = objective(best);
    std::vector<double> cand(n);
    for (int s = 0; s < SAMPLES; s++) {
        double sum = 0;
        for (std::size_t i = 0; i < n; i++) {
            cand[i] = -std::log(rnd.nextDouble());
            sum += cand[i];
        }
        for (std::size_t i = 0; i < n; i++) {
            cand[i] /= sum;
        }
        double score = objective(cand);
        if (score > bestScore) {
            bestScore = score;
            best = cand;
        }
    }

    // Phase 2: deterministic pairwise-transfer hill climbing, shrinking step.
    double step = 0.10;
    std::vector<double> trial(n);
    for (int sweep = 0; sweep < REFINE_SWEEPS; sweep++) {
        bool improved = false;
        for (std::size_t from = 0; from < n; from++) {
            if (best[from] < 1e-12) {
                continue;
            }
            for (std::size_t to = 0; to < n; to++) {
                if (to == from) {
                    continue;
                }
                double move = std::min(step, best[from]);
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

double PortfolioOptimizer::volatilityOf(const std::vector<double>& w) const {
    return std::sqrt(std::max(0.0, math_utils::quadraticForm(w, covariance_)));
}

Allocation PortfolioOptimizer::toAllocation(std::vector<double> w,
                                            double riskFreeRate) const {
    double ret = math_utils::dot(expectedReturns_, w);
    double vol = volatilityOf(w);
    double sharpe = vol == 0 ? 0 : (ret - riskFreeRate) / vol;
    Allocation a;
    a.weights = std::move(w);
    a.expectedReturn = ret;
    a.volatility = vol;
    a.sharpe = sharpe;
    return a;
}

}  // namespace quantfinlib
