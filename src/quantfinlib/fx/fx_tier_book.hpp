// Port of com.quantfinlib.fx.FxTierBook (Java): tiered multi-LP FX book —
// each LP streams a private ladder of size tiers (1M at 1.08500/02, 5M at
// 1.08498/04, ...). Answers per-clip questions: what does a sweep of N
// actually cost (sweepBuyCost/sweepSellProceeds), and which single LP fills
// the whole clip at one price (bestFullAmountAsk/bestFullAmountBid — the
// full-amount convention that avoids signaling with a spray of children).
// Zero allocation: fixed lpCount x maxTiers primitive arrays, caller-owned
// scratch for sweep plans, single writer. Tiers are best-first per LP
// (tier 0 = tightest); a dealable price/size is strictly positive — NaN,
// zero, or negative never wins (the frontier/sweep convention).
#pragma once

#include <vector>

namespace quantfinlib {

class FxTierBook {
public:
    FxTierBook(int lpCount, int maxTiers);

    /**
     * Replaces one tier of one LP's ladder (tier < maxTiers). Call
     * tierCount after the last tier of an update.
     */
    void tier(int lp, bool bid, int tier, double price, double size);

    /** Declares how many tiers of lp's side are now active (0 pulls the side). */
    void tierCount(int lp, bool bid, int count);

    /** Pulls an LP entirely (disconnect / last-look withdrawal). */
    void clear(int lp);

    /** Best (highest) bid across LPs at each LP's frontier tier; NaN when nobody bids. */
    double bestBid() const;

    /** Best (lowest) ask across LPs at each LP's frontier tier; NaN when nobody offers. */
    double bestAsk() const;

    /**
     * All-in cost of BUYING size by sweeping ask tiers across LPs, cheapest
     * first. NaN when the book cannot fill the size.
     */
    double sweepBuyCost(double size) const;

    /** Mirror: proceeds of SELLING size into the bid tiers; NaN if unfillable. */
    double sweepSellProceeds(double size) const;

    /**
     * Sweep with a plan: outLpQty[lp] receives the quantity taken from each
     * LP (length >= lpCount, fully overwritten).
     */
    double sweepPlan(bool buy, double size, std::vector<double>& outLpQty) const;

    /** Best single-LP full-amount ASK for size; NaN when no LP quotes it. */
    double bestFullAmountAsk(double size) const;

    /** Mirror on the bid side. */
    double bestFullAmountBid(double size) const;

    /** LP index behind bestFullAmountAsk; -1 when no LP covers the size. */
    int bestFullAmountAskLp(double size) const;

    /** LP index behind bestFullAmountBid; -1 when none. */
    int bestFullAmountBidLp(double size) const;

    /**
     * One LP's full-amount price for a clip: the tightest tier whose clip
     * capacity covers size, NaN when the LP doesn't quote it.
     */
    double fullAmountPrice(int lp, bool buy, double size) const;

    int lpCount() const { return lpCount_; }
    int maxTiers() const { return maxTiers_; }
    int tierCount(int lp, bool bid) const { return bid ? bidTiers_[lp] : askTiers_[lp]; }
    double price(int lp, bool bid, int tier) const;
    double size(int lp, bool bid, int tier) const;
    long long updateCount() const { return updateCount_; }

private:
    double sweep(double size, bool buy, std::vector<double>* outLpQty) const;
    double fullAmount(double size, bool buy) const;
    int fullAmountLp(double size, bool buy) const;

    /**
     * First tier at or after `from` with a dealable price and positive
     * size; n = exhausted.
     */
    static int frontier(int lp, const std::vector<double>& px, const std::vector<double>& sz,
                        int n, int from, int maxTiers);

    int lpCount_;
    int maxTiers_;

    std::vector<double> bidPx_;
    std::vector<double> bidSz_;
    std::vector<double> askPx_;
    std::vector<double> askSz_;
    std::vector<int> bidTiers_;
    std::vector<int> askTiers_;

    // Sweep frontier scratch (mutable: single-writer, reused across const calls).
    mutable std::vector<int> sweepTier_;
    mutable std::vector<double> sweepRem_;

    mutable double lastFullAmountPrice_;
    long long updateCount_;
};

}  // namespace quantfinlib
