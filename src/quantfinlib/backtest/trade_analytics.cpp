#include "quantfinlib/backtest/trade_analytics.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace quantfinlib {
namespace trade_analytics {

namespace {
int countLosers(const std::vector<Trade>& trades) {
    int losers = 0;
    for (const Trade& t : trades) {
        if (t.pnl < 0) {
            losers++;
        }
    }
    return losers;
}
}  // namespace

Result analyze(const std::vector<Trade>& trades) {
    int n = static_cast<int>(trades.size());
    if (n == 0) {
        throw std::invalid_argument("need at least one trade");
    }
    int wins = 0;
    double winSum = 0, lossSum = 0;
    std::int64_t winBars = 0, lossBars = 0;
    int maxWin = 0, maxLoss = 0, curWin = 0, curLoss = 0;
    for (const Trade& t : trades) {
        if (!std::isfinite(t.pnl)) {
            throw std::invalid_argument("non-finite trade pnl");
        }
        if (t.pnl > 0) {
            wins++;
            winSum += t.pnl;
            winBars += t.barsHeld();
            curWin++;
            curLoss = 0;
            maxWin = std::max(maxWin, curWin);
        } else if (t.pnl < 0) {
            lossSum += -t.pnl;
            lossBars += t.barsHeld();
            curLoss++;
            curWin = 0;
            maxLoss = std::max(maxLoss, curLoss);
        } else {
            // Scratch trade breaks both streaks; belongs to neither leg.
            curWin = 0;
            curLoss = 0;
        }
    }
    int losers = countLosers(trades);
    double winRate = static_cast<double>(wins) / n;
    double avgWin = wins > 0 ? winSum / wins : 0;
    double avgLoss = losers > 0 ? lossSum / losers : 0;
    double lossRate = static_cast<double>(losers) / n;
    double expectancy = winRate * avgWin - lossRate * avgLoss;
    double payoff = avgLoss > 0 ? avgWin / avgLoss
                                : std::numeric_limits<double>::infinity();
    // Kelly = W - (1-W)/R, clamped: no losers -> bet everything (the
    // over-fit tell); non-positive edge -> zero.
    double kelly;
    if (avgLoss == 0) {
        kelly = 1.0;
    } else {
        kelly = winRate - (1 - winRate) / payoff;
        kelly = std::max(0.0, std::min(1.0, kelly));
    }
    double avgWinBars = wins > 0 ? static_cast<double>(winBars) / wins : 0;
    double avgLossBars = losers > 0 ? static_cast<double>(lossBars) / losers : 0;
    Result r;
    r.count = n;
    r.winRate = winRate;
    r.expectancy = expectancy;
    r.avgWin = avgWin;
    r.avgLoss = avgLoss;
    r.payoffRatio = payoff;
    r.maxWinStreak = maxWin;
    r.maxLossStreak = maxLoss;
    r.kellyFraction = kelly;
    r.avgBarsHeldWinners = avgWinBars;
    r.avgBarsHeldLosers = avgLossBars;
    return r;
}

}  // namespace trade_analytics
}  // namespace quantfinlib
