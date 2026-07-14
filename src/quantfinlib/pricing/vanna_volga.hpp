// Port of com.quantfinlib.pricing.VannaVolga (Java): the FX desk's
// standard smile-consistent adjustment built from exactly three market
// pillars (25-delta put, ATM, 25-delta call). Hedge the flat-vol BS
// price's vega, vanna and volga with the three pillars; the market cost
// of that hedge is the smile adjustment:
//   price(K) = BS(K; atm) + sum_i w_i(K) * [BS(K_i; vol_i) - BS(K_i; atm)]
// with the classic log-strike weights — exact AT the pillars, smooth
// between and beyond them. impliedVol() inverts the adjusted price back
// through Black-Scholes.
#pragma once

#include <vector>

#include "quantfinlib/pricing/black_scholes.hpp"

namespace quantfinlib {

class VannaVolga {
public:
    /**
     * @param strikes three ascending pillar strikes (25d put, ATM, 25d call)
     * @param vols    their market vols; vols[1] is the ATM anchor
     */
    VannaVolga(const std::vector<double>& strikes, const std::vector<double>& vols,
               double rate, double carry, double timeYears);

    /**
     * Builds from a solved smile pillar set; a five-pillar smile (10-delta
     * wings present) reduces to the classic 25-delta triple (indices 1..3).
     */
    static VannaVolga ofPillars(const std::vector<double>& strikes,
                                const std::vector<double>& vols, double rate,
                                double carry, double timeYears);

    /** Smile-consistent price of a vanilla at any strike. */
    double price(OptionType type, double spot, double strike) const;

    /**
     * Smile-consistent implied vol at any strike: the vanna-volga price
     * inverted through Black-Scholes. Pillar strikes recover their market
     * vols exactly.
     */
    double impliedVol(double spot, double strike) const;

private:
    /** The classic log-strike interpolation weights (exact at the pillars). */
    double weight(int i, double k, double vegaK, double vegaI) const;

    double strikes_[3];  // ascending: put wing, ATM, call wing
    double vols_[3];     // market vols at the pillars
    double rate_;
    double carry_;
    double timeYears_;
};

}  // namespace quantfinlib
