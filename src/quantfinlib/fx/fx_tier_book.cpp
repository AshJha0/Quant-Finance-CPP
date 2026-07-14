#include "quantfinlib/fx/fx_tier_book.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {
constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();
}

FxTierBook::FxTierBook(int lpCount, int maxTiers)
    : lpCount_(lpCount),
      maxTiers_(maxTiers),
      bidPx_(static_cast<std::size_t>(lpCount) * maxTiers, 0.0),
      bidSz_(static_cast<std::size_t>(lpCount) * maxTiers, 0.0),
      askPx_(static_cast<std::size_t>(lpCount) * maxTiers, 0.0),
      askSz_(static_cast<std::size_t>(lpCount) * maxTiers, 0.0),
      bidTiers_(lpCount, 0),
      askTiers_(lpCount, 0),
      sweepTier_(lpCount, 0),
      sweepRem_(lpCount, 0.0),
      lastFullAmountPrice_(kNaN),
      updateCount_(0) {
    if (lpCount < 1 || maxTiers < 1) {
        throw std::invalid_argument("need lpCount >= 1, maxTiers >= 1");
    }
}

void FxTierBook::tier(int lp, bool bid, int t, double price, double size) {
    if (lp < 0 || lp >= lpCount_ || t < 0 || t >= maxTiers_) {
        throw std::invalid_argument("lp/tier out of range");
    }
    int i = lp * maxTiers_ + t;
    if (bid) {
        bidPx_[i] = price;
        bidSz_[i] = size;
    } else {
        askPx_[i] = price;
        askSz_[i] = size;
    }
}

void FxTierBook::tierCount(int lp, bool bid, int count) {
    if (count < 0 || count > maxTiers_) {
        throw std::invalid_argument("tier count out of range");
    }
    if (bid) {
        bidTiers_[lp] = count;
    } else {
        askTiers_[lp] = count;
    }
    updateCount_++;
}

void FxTierBook::clear(int lp) {
    bidTiers_[lp] = 0;
    askTiers_[lp] = 0;
    updateCount_++;
}

int FxTierBook::frontier(int lp, const std::vector<double>& px, const std::vector<double>& sz,
                        int n, int from, int maxTiers) {
    int base = lp * maxTiers;
    for (int t = from; t < n; t++) {
        if (px[base + t] > 0 && sz[base + t] > 0) {
            return t;
        }
    }
    return n;
}

double FxTierBook::bestBid() const {
    double best = kNaN;
    for (int lp = 0; lp < lpCount_; lp++) {
        int t = frontier(lp, bidPx_, bidSz_, bidTiers_[lp], 0, maxTiers_);
        if (t < bidTiers_[lp]) {
            double p = bidPx_[lp * maxTiers_ + t];
            if (std::isnan(best) || p > best) {
                best = p;
            }
        }
    }
    return best;
}

double FxTierBook::bestAsk() const {
    double best = kNaN;
    for (int lp = 0; lp < lpCount_; lp++) {
        int t = frontier(lp, askPx_, askSz_, askTiers_[lp], 0, maxTiers_);
        if (t < askTiers_[lp]) {
            double p = askPx_[lp * maxTiers_ + t];
            if (std::isnan(best) || p < best) {
                best = p;
            }
        }
    }
    return best;
}

double FxTierBook::sweep(double size, bool buy, std::vector<double>* outLpQty) const {
    if (outLpQty != nullptr) {
        std::fill(outLpQty->begin(), outLpQty->begin() + lpCount_, 0.0);
    }
    if (size <= 0) {
        return kNaN;
    }
    const std::vector<double>& px = buy ? askPx_ : bidPx_;
    const std::vector<double>& sz = buy ? askSz_ : bidSz_;
    const std::vector<int>& tiers = buy ? askTiers_ : bidTiers_;
    for (int lp = 0; lp < lpCount_; lp++) {
        sweepTier_[lp] = frontier(lp, px, sz, tiers[lp], 0, maxTiers_);
        sweepRem_[lp] = sweepTier_[lp] < tiers[lp] ? sz[lp * maxTiers_ + sweepTier_[lp]] : 0;
    }
    double remaining = size;
    double notional = 0;
    while (remaining > 0) {
        int best = -1;
        double bestPx = 0;
        for (int lp = 0; lp < lpCount_; lp++) {
            if (sweepTier_[lp] >= tiers[lp]) {
                continue;
            }
            double p = px[lp * maxTiers_ + sweepTier_[lp]];
            if (best == -1 || (buy ? p < bestPx : p > bestPx)) {
                best = lp;
                bestPx = p;
            }
        }
        if (best == -1) {
            return kNaN;  // book too shallow for the size
        }
        double take = std::min(remaining, sweepRem_[best]);
        sweepRem_[best] -= take;
        notional += take * bestPx;
        remaining -= take;
        if (outLpQty != nullptr) {
            (*outLpQty)[best] += take;
        }
        if (sweepRem_[best] <= 0) {
            sweepTier_[best] = frontier(best, px, sz, tiers[best], sweepTier_[best] + 1, maxTiers_);
            if (sweepTier_[best] < tiers[best]) {
                sweepRem_[best] = sz[best * maxTiers_ + sweepTier_[best]];
            }
        }
    }
    return notional;
}

double FxTierBook::sweepBuyCost(double size) const {
    return sweep(size, true, nullptr);
}

double FxTierBook::sweepSellProceeds(double size) const {
    return sweep(size, false, nullptr);
}

double FxTierBook::sweepPlan(bool buy, double size, std::vector<double>& outLpQty) const {
    return sweep(size, buy, &outLpQty);
}

double FxTierBook::bestFullAmountAsk(double size) const {
    return fullAmount(size, true);
}

double FxTierBook::bestFullAmountBid(double size) const {
    return fullAmount(size, false);
}

int FxTierBook::bestFullAmountAskLp(double size) const {
    return fullAmountLp(size, true);
}

int FxTierBook::bestFullAmountBidLp(double size) const {
    return fullAmountLp(size, false);
}

double FxTierBook::fullAmount(double size, bool buy) const {
    fullAmountLp(size, buy);  // also records the price
    return lastFullAmountPrice_;
}

int FxTierBook::fullAmountLp(double size, bool buy) const {
    int bestLp = -1;
    double bestPx = 0;
    for (int lp = 0; lp < lpCount_; lp++) {
        double p = fullAmountPrice(lp, buy, size);
        if (std::isnan(p)) {
            continue;
        }
        if (bestLp == -1 || (buy ? p < bestPx : p > bestPx)) {
            bestLp = lp;
            bestPx = p;
        }
    }
    lastFullAmountPrice_ = bestLp == -1 ? kNaN : bestPx;
    return bestLp;
}

double FxTierBook::fullAmountPrice(int lp, bool buy, double size) const {
    if (size <= 0) {
        return kNaN;
    }
    int base = lp * maxTiers_;
    int n = buy ? askTiers_[lp] : bidTiers_[lp];
    const std::vector<double>& px = buy ? askPx_ : bidPx_;
    const std::vector<double>& sz = buy ? askSz_ : bidSz_;
    for (int t = 0; t < n; t++) {
        if (sz[base + t] >= size && px[base + t] > 0) {
            return px[base + t];
        }
    }
    return kNaN;
}

double FxTierBook::price(int lp, bool bid, int tier) const {
    int i = lp * maxTiers_ + tier;
    return bid ? bidPx_[i] : askPx_[i];
}

double FxTierBook::size(int lp, bool bid, int tier) const {
    int i = lp * maxTiers_ + tier;
    return bid ? bidSz_[i] : askSz_[i];
}

}  // namespace quantfinlib
