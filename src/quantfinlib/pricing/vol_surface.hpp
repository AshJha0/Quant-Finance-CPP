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

        /** Throws std::runtime_error (Java IllegalStateException) when empty. */
        VolSurface build() const;

    private:
        std::map<double, std::map<double, double>> smiles_;
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

    std::vector<double> expiries() const;

    /** Pillar strikes at an exact quoted expiry; throws if the expiry is unknown. */
    std::vector<double> strikes(double expiryYears) const;

private:
    explicit VolSurface(std::map<double, std::map<double, double>> smiles);

    /** Linear in strike inside the smile; flat beyond the quoted wings. */
    static double smileVol(const std::map<double, double>& smile, double strike);

    std::map<double, std::map<double, double>> smiles_;  // expiry -> (strike -> vol)
};

}  // namespace quantfinlib
