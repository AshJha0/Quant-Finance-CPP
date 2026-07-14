#include "quantfinlib/execution/ucb1_selector.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

Ucb1Selector::Ucb1Selector(int arms) {
    if (arms < 2) {
        throw std::invalid_argument("a one-armed bandit is not a decision");
    }
    rewardSums_.assign(static_cast<std::size_t>(arms), 0.0);
    pulls_.assign(static_cast<std::size_t>(arms), 0);
}

int Ucb1Selector::select() const {
    for (std::size_t i = 0; i < pulls_.size(); i++) {
        if (pulls_[i] == 0) {
            return static_cast<int>(i);  // every arm earns one look
        }
    }
    int best = 0;
    double bestUcb = -std::numeric_limits<double>::infinity();
    double logN = std::log(static_cast<double>(totalPulls_));
    for (std::size_t i = 0; i < pulls_.size(); i++) {
        double ucb = rewardSums_[i] / static_cast<double>(pulls_[i])
                + std::sqrt(2 * logN / static_cast<double>(pulls_[i]));
        if (ucb > bestUcb) {
            bestUcb = ucb;
            best = static_cast<int>(i);
        }
    }
    return best;
}

void Ucb1Selector::record(int arm, double reward) {
    if (arm < 0 || arm >= static_cast<int>(pulls_.size())) {
        throw std::invalid_argument("arm " + std::to_string(arm) + " of " + std::to_string(pulls_.size()));
    }
    if (!(reward >= 0 && reward <= 1)) {
        throw std::invalid_argument("reward must be in [0, 1], got " + std::to_string(reward)
                                     + " -- a mis-scaled reward silently breaks the exploration balance");
    }
    auto a = static_cast<std::size_t>(arm);
    rewardSums_[a] += reward;
    pulls_[a]++;
    totalPulls_++;
}

double Ucb1Selector::meanReward(int arm) const {
    auto a = static_cast<std::size_t>(arm);
    return pulls_[a] == 0 ? std::nan("") : rewardSums_[a] / static_cast<double>(pulls_[a]);
}

}  // namespace quantfinlib
