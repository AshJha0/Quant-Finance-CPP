// Port of com.quantfinlib.fx.FxVolSurface (Java): FX-style volatility
// surface built from the market's delta-quoted smile — ATM (delta-neutral
// straddle), 25-delta risk reversal and butterfly, optionally 10-delta
// wings. Converts broker quotes to five pillar vols:
//   vol(25D call) = atm + bf25 + rr25/2
//   vol(25D put)  = atm + bf25 - rr25/2      (same shape at 10D)
// then solves each pillar's strike from its delta and its own vol.
// Delta convention: forward delta (N(d1)) by default; premium-adjusted
// forward delta ((K/F)*N(d2)) with Builder::premiumAdjusted(true) — the
// convention for pairs whose premium is paid in base currency (USDJPY).
// Within an expiry: linear in vol against log-moneyness, flat beyond the
// wings. Across expiries: linear in total variance, flat outside the
// quoted range. Lookups after build() are allocation-free.
#pragma once

#include <vector>

namespace quantfinlib {

class FxVolSurface {
public:
    /** One expiry's solved smile: absolute strikes and vols, low to high strike. */
    struct SmilePillar {
        double expiryYears;
        double forward;
        std::vector<double> strikes;
        std::vector<double> vols;
    };

    /** Accumulates per-expiry delta quotes, then solves strikes once in build(). */
    class Builder {
    public:
        Builder() = default;

        /** 25D-only smile quote (rr/bf in absolute vol, e.g. 0.01 = 1 vol point). */
        Builder& add(double expiryYears, double forward, double atmVol,
                    double rr25, double bf25);

        /** Full five-pillar quote with 10D wings. */
        Builder& add(double expiryYears, double forward, double atmVol,
                    double rr25, double bf25, double rr10, double bf10);

        /** Switches strike solving to premium-adjusted forward delta. */
        Builder& premiumAdjusted(bool value);

        FxVolSurface build() const;

    private:
        struct Quote {
            double t, forward, atm, rr25, bf25, rr10, bf10;
        };
        std::vector<Quote> quotes_;
        bool premiumAdjusted_ = false;
    };

    static Builder builder() { return Builder(); }

    /**
     * Delta-neutral-straddle (ATM) strike: F*e^{+sigma^2*tau/2} for forward
     * delta, F*e^{-sigma^2*tau/2} premium-adjusted.
     */
    static double dnsStrike(double forward, double vol, double tYears, bool premiumAdjusted);

    /**
     * Strike for a target forward delta (call delta in (0,1), put delta in
     * (-1,0)). Unadjusted deltas invert in closed form; premium-adjusted
     * deltas are solved by bisection on the OTM branch.
     */
    static double strikeForDelta(double forward, double vol, double tYears, double delta,
                                 bool isCall, bool premiumAdjusted);

    /**
     * Interpolated vol for an absolute strike at an expiry. Wings are flat
     * beyond the quoted pillars; time interpolation is linear in total
     * variance and flat outside the quoted expiries.
     */
    double vol(double expiryYears, double strike) const;

    /** ATM (delta-neutral straddle) vol at an expiry: the smile at zero skew. */
    double atmVol(double expiryYears) const;

    /** Log-linear interpolated forward at an expiry, flat outside pillars. */
    double forwardAt(double expiryYears) const;

    /** The solved pillar smile at index i (reporting, VannaVolga inputs). */
    SmilePillar pillar(int i) const;

    int pillarCount() const { return static_cast<int>(expiries_.size()); }
    bool isPremiumAdjusted() const { return premiumAdjusted_; }

private:
    FxVolSurface(std::vector<double> expiries, std::vector<double> forwards,
                std::vector<std::vector<double>> logMoneyness,
                std::vector<std::vector<double>> vols, bool premiumAdjusted);

    /** Binary search: largest pillar index with expiry <= t (interior t only). */
    int bracket(double t) const;

    /** Linear interp in log-moneyness within expiry i, flat wings. */
    double smileVol(int i, double x) const;

    int nearestExpiry(double t) const;

    std::vector<double> expiries_;                    // ascending
    std::vector<double> forwards_;                     // per expiry
    std::vector<std::vector<double>> logMoneyness_;    // per expiry, ascending ln(K/F)
    std::vector<std::vector<double>> vols_;            // per expiry, aligned with logMoneyness
    bool premiumAdjusted_;
};

}  // namespace quantfinlib
