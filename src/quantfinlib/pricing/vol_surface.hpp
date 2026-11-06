// Port of com.quantfinlib.pricing.VolSurface (Java): implied volatility
// surface built from (expiry, strike, vol) pillar quotes — or directly
// from market option prices via implied-vol inversion. Interpolation
// follows market practice: linear in vol across strikes within a smile
// (flat beyond the wings), linear in TOTAL VARIANCE (w = vol^2 T) at
// fixed strike across expiries (flat vol outside the quoted range).
// TreeMap becomes std::map. Immutable once built.
#pragma once

#include <map>
#include <vector>

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {

class VolSurface {
public:
    class Builder {
    public:
        /** Adds one pillar quote. NaN pillars are rejected at the door. */
        Builder& add(double expiryYears, double strike, double vol);

        /** Adds a pillar from a market option price via implied-vol inversion. */
        Builder& addFromPrice(OptionType type, double marketPrice, double spot,
                              double strike, double rate, double carry,
                              double expiryYears);

        /**
         * The forward at a quoted expiry. When any are given, all quoted
         * expiries need one, and the calendar scan compares at equal
         * log-moneyness rather than equal strike.
         */
        Builder& forward(double expiryYears, double forwardValue);

        /**
         * Opt-in: build() throws std::invalid_argument on calendar arbitrage
         * (scanned on gridPoints) and, for expiries with a forward, on
         * butterfly arbitrage between quoted strikes: undiscounted call
         * prices must be decreasing and convex across the pillars. Pillars are
         * tested rather than the interpolated smile, whose kinks are artefacts.
         */
        Builder& rejectArbitrage(int gridPoints = 41);

        /** Throws std::runtime_error (Java IllegalStateException) when empty. */
        VolSurface build() const;

    private:
        std::map<double, std::map<double, double>> smiles_;
        std::map<double, double> forwards_;
        int arbitrageGrid_ = 0;  // 0 = no check at build
    };

    /** Total variance falls between two expiries at this log-moneyness. */
    struct CalendarViolation {
        double expiry1;
        double expiry2;
        double logMoneyness;
        double totalVariance1;
        double totalVariance2;
    };

    static Builder builder();

    /** Interpolated implied volatility at any (expiry, strike). */
    double vol(double expiryYears, double strike) const;

    /** ATM vol, taking the forward (or spot) as the at-the-money strike. */
    double atmVol(double expiryYears, double forward) const;

    /** Option price using the surface vol at (expiry, strike). */
    double price(OptionType type, double spot, double strike,
                 double rate, double carry, double expiryYears) const;

    /** Smile slope between two strikes, in vol points per unit of strike. */
    double skew(double expiryYears, double strikeLow, double strikeHigh) const;

    /**
     * Calendar-arbitrage scan: for each pair of consecutive quoted expiries,
     * total variance must not fall at any of gridPoints log-moneyness points
     * spanning both smiles. Empty means none found on the grid. The surface
     * itself enforces nothing.
     */
    std::vector<CalendarViolation> calendarViolations(int gridPoints) const;

    /**
     * Smallest butterfly density d2C/dK2 (undiscounted Black call) over n
     * strikes of one quoted smile. Negative means butterfly arbitrage. Kinks
     * of the linear interpolation show as spikes; the slope change itself is
     * arbitrageable.
     */
    double minimumDensity(double expiryYears, double forward, int n) const;

    std::vector<double> expiries() const;

    /** Pillar strikes at an exact quoted expiry; throws if the expiry is unknown. */
    std::vector<double> strikes(double expiryYears) const;

private:
    VolSurface(std::map<double, std::map<double, double>> smiles,
               std::map<double, double> forwards);

    double logMoneyness(double t, double strike) const;
    double fromMoneyness(double t, double x) const;

    /** Linear in strike inside the smile; flat beyond the quoted wings. */
    static double smileVol(const std::map<double, double>& smile, double strike);

    std::map<double, std::map<double, double>> smiles_;  // expiry -> (strike -> vol)
    std::map<double, double> forwards_;                  // expiry -> forward, or empty
};

}  // namespace quantfinlib
