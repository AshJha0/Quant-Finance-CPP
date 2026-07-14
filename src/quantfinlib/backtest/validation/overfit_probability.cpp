#include "quantfinlib/backtest/validation/overfit_probability.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace overfit_probability {

namespace {

constexpr int MAX_BLOCKS = 16;  // C(16,8) = 12,870 combinations

double score(const std::vector<std::vector<double>>& returns,
             const std::vector<bool>& inSample, bool wantIs, int blockLen, int half,
             int variant, const std::function<double(const std::vector<double>&)>& objective) {
    std::vector<double> series(static_cast<std::size_t>(half) * blockLen);
    int w = 0;
    for (int b = 0; b < static_cast<int>(inSample.size()); b++) {
        if (inSample[static_cast<std::size_t>(b)] == wantIs) {
            int from = b * blockLen;
            for (int t = 0; t < blockLen; t++) {
                series[static_cast<std::size_t>(w++)] =
                        returns[static_cast<std::size_t>(from + t)][static_cast<std::size_t>(variant)];
            }
        }
    }
    double s = objective(series);
    // Finite required, not merely non-NaN: an all -infinity in-sample
    // column would leave the argmax with no winner at all.
    if (!std::isfinite(s)) {
        throw std::invalid_argument("objective returned non-finite " + std::to_string(s)
                                    + " for variant " + std::to_string(variant));
    }
    return s;
}

/** All C(blocks, half) ascending index combinations, lexicographic. */
void enumerate(std::vector<int>& pick, int pos, int from, int blocks,
               std::vector<std::vector<int>>& out) {
    if (pos == static_cast<int>(pick.size())) {
        out.push_back(pick);
        return;
    }
    for (int b = from; b <= blocks - (static_cast<int>(pick.size()) - pos); b++) {
        pick[static_cast<std::size_t>(pos)] = b;
        enumerate(pick, pos + 1, b + 1, blocks, out);
    }
}

void validate(const std::vector<std::vector<double>>& returns, int blocks) {
    if (blocks < 4 || blocks % 2 != 0 || blocks > MAX_BLOCKS) {
        throw std::invalid_argument("blocks must be even, in [4, "
                + std::to_string(MAX_BLOCKS) + "], got " + std::to_string(blocks));
    }
    if (static_cast<int>(returns.size()) < 2 * blocks) {
        throw std::invalid_argument("need >= 2 periods per block: "
                + std::to_string(returns.size()) + " periods for "
                + std::to_string(blocks) + " blocks");
    }
    std::size_t nVariants = returns[0].size();
    if (nVariants < 2) {
        throw std::invalid_argument("need >= 2 strategy variants, got "
                                    + std::to_string(nVariants));
    }
    for (std::size_t t = 0; t < returns.size(); t++) {
        if (returns[t].size() != nVariants) {
            throw std::invalid_argument("ragged matrix: row " + std::to_string(t) + " has "
                    + std::to_string(returns[t].size()) + " variants, row 0 has "
                    + std::to_string(nVariants));
        }
        for (std::size_t j = 0; j < nVariants; j++) {
            if (!std::isfinite(returns[t][j])) {
                throw std::invalid_argument("non-finite return at period " + std::to_string(t)
                                            + ", variant " + std::to_string(j));
            }
        }
    }
}

}  // namespace

Result cscv(const std::vector<std::vector<double>>& returns, int blocks,
            const std::function<double(const std::vector<double>&)>& objective) {
    validate(returns, blocks);
    int nVariants = static_cast<int>(returns[0].size());
    int blockLen = static_cast<int>(returns.size()) / blocks;
    int half = blocks / 2;

    std::vector<std::vector<int>> combos;
    std::vector<int> pick(static_cast<std::size_t>(half));
    enumerate(pick, 0, 0, blocks, combos);

    std::vector<double> logits(combos.size());
    std::vector<bool> inSample(static_cast<std::size_t>(blocks));
    int below = 0;
    for (std::size_t c = 0; c < combos.size(); c++) {
        std::fill(inSample.begin(), inSample.end(), false);
        for (int b : combos[c]) {
            inSample[static_cast<std::size_t>(b)] = true;
        }
        // Score every variant IS and OOS on this split.
        double bestIs = -std::numeric_limits<double>::infinity();
        int winner = -1;
        std::vector<double> oosScores(static_cast<std::size_t>(nVariants));
        for (int j = 0; j < nVariants; j++) {
            double is = score(returns, inSample, true, blockLen, half, j, objective);
            oosScores[static_cast<std::size_t>(j)] =
                    score(returns, inSample, false, blockLen, half, j, objective);
            if (is > bestIs) {
                bestIs = is;
                winner = j;
            }
        }
        int rank = 1;
        for (int j = 0; j < nVariants; j++) {
            if (oosScores[static_cast<std::size_t>(j)]
                    < oosScores[static_cast<std::size_t>(winner)]) {
                rank++;
            }
        }
        double w = static_cast<double>(rank) / (nVariants + 1);
        double lambda = std::log(w / (1 - w));
        logits[c] = lambda;
        if (lambda <= 0) {
            below++;
        }
    }
    Result r;
    r.pbo = static_cast<double>(below) / static_cast<double>(combos.size());
    r.combinations = static_cast<int>(combos.size());
    r.logits = std::move(logits);
    return r;
}

Result cscvSharpe(const std::vector<std::vector<double>>& returns, int blocks) {
    return cscv(returns, blocks, [](const std::vector<double>& r) {
        double sd = math_utils::stdDev(r);
        return sd > 0 ? math_utils::mean(r) / sd : 0;
    });
}

}  // namespace overfit_probability
}  // namespace quantfinlib
