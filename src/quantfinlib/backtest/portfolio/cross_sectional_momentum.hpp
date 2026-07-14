// Port of com.quantfinlib.backtest.portfolio.CrossSectionalMomentum:
// cross-sectional momentum — the classic equity factor. At every
// rebalance the strategy ranks candidates by trailing performance, goes
// long the best performers and short the worst.
//
// Momentum definition is the academic standard "12-1":
// close(i - skip) / close(i - lookback) - 1 — trailing lookback bars
// with the most recent skip bars excluded (short-term reversal
// contaminates raw 12-month momentum; Jegadeesh-Titman 1993). Each side
// is equal-weighted at grossPerSide total, so the default book is
// dollar-neutral with 2x gross-per-side exposure.
//
// PORT SCOPE: the Java class is POINT-IN-TIME — it ranks only index
// members via PointInTimeUniverse, which has no C++ port yet. This port
// is the null-universe behavior (everything always tradeable), which
// the Java class documents as "useful for measuring the bias".
// Candidates also need lookback bars of history at the rebalance bar;
// earlier bars produce an empty book (the engine holds cash). Gates map
// to std::invalid_argument.
#pragma once

#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "quantfinlib/backtest/portfolio/portfolio_strategy.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

class CrossSectionalMomentum final : public PortfolioStrategy {
public:
    /**
     * @param lookbackBars momentum window (252 ~ 12 months of dailies)
     * @param skipBars     most-recent bars excluded (21 ~ 1 month)
     * @param perSide      names held long and short (shrinks when the
     *                     symbol count can't fill both sides without
     *                     overlap)
     * @param grossPerSide total absolute weight per side (0.5 -> 1x
     *                     gross, dollar-neutral)
     */
    struct Config {
        int lookbackBars = 252;
        int skipBars = 21;
        int perSide = 1;
        double grossPerSide = 0.5;

        Config(int lookback, int skip, int per, double gross)
                : lookbackBars(lookback), skipBars(skip), perSide(per), grossPerSide(gross) {
            if (lookbackBars <= 0 || skipBars < 0 || skipBars >= lookbackBars || perSide <= 0
                || grossPerSide <= 0) {
                throw std::invalid_argument(
                        "need lookback > skip >= 0, perSide > 0, grossPerSide > 0");
            }
        }

        /** The academic 12-1 monthly-rebalance setup on daily bars. */
        static Config twelveMinusOne(int perSide) { return Config(252, 21, perSide, 0.5); }
    };

    explicit CrossSectionalMomentum(const Config& config) : config_(config) {}

    std::string name() const override {
        return "XS_MOMENTUM(" + std::to_string(config_.lookbackBars) + "-"
               + std::to_string(config_.skipBars) + ", " + std::to_string(config_.perSide)
               + "/side)";
    }

    void init(const std::map<std::string, BarSeries>& data) override {
        data_ = &data;
        symbols_.clear();
        for (const auto& e : data) {
            symbols_.push_back(e.first);
        }
    }

    std::map<std::string, double> targetWeights(int index) override {
        if (index < config_.lookbackBars) {
            return {};  // not enough history: hold cash
        }
        // Rank every symbol (null-universe behavior — see the header
        // comment). Primitive vectors + math_utils::pairSort keep the
        // Java version's no-boxed-comparator ranking.
        int n = 0;
        std::vector<double> momentum(symbols_.size(), 0.0);
        std::vector<int> candidate(symbols_.size(), 0);  // indices into symbols_
        for (std::size_t i = 0; i < symbols_.size(); i++) {
            const BarSeries& s = data_->at(symbols_[i]);
            double past = s.close(index - config_.lookbackBars);
            double recent = s.close(index - config_.skipBars);
            momentum[static_cast<std::size_t>(n)] = recent / past - 1;
            candidate[static_cast<std::size_t>(n)] = static_cast<int>(i);
            n++;
        }
        // Both sides need at least one name each without overlapping.
        int side = std::min(config_.perSide, n / 2);
        if (side == 0) {
            return {};
        }

        // Ascending sort: losers at the front, winners at the back.
        std::vector<double> keys(momentum.begin(), momentum.begin() + n);
        std::vector<int> order(candidate.begin(), candidate.begin() + n);
        math_utils::pairSort(keys, order);

        std::map<std::string, double> weights;
        double w = config_.grossPerSide / side;
        for (int i = 0; i < side; i++) {
            weights[symbols_[static_cast<std::size_t>(order[static_cast<std::size_t>(
                    n - 1 - i)])]] = w;                                            // winners long
            weights[symbols_[static_cast<std::size_t>(order[static_cast<std::size_t>(i)])]] =
                    -w;                                                            // losers short
        }
        return weights;
    }

private:
    Config config_;
    const std::map<std::string, BarSeries>* data_ = nullptr;
    std::vector<std::string> symbols_;
};

}  // namespace quantfinlib
