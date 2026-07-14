// Port of com.quantfinlib.pricing.Autocallable (Java): autocallable note
// pricer — a note that pays a fat coupon and redeems early the first
// observation date the underlier closes at or above the autocall barrier.
// If it survives to maturity, principal is protected UNLESS the underlier
// fell through the (European, maturity-observed) knock-in barrier — the
// note is a bond plus sold down-and-in put, funded by the coupons.
// Phoenix memory coupons optional. Monte Carlo under flat-vol GBM with
// antithetic variates on a bit-exact java.util.Random stream: a fixed
// seed makes every price reproducible and every zero-vol test exact.
// Documented simplifications (as in Java): flat vol/rates, observation-
// date monitoring, no issuer credit spread.
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {

class Autocallable {
public:
    /**
     * @param notional         redemption amount, e.g. 1'000'000
     * @param observationYears strictly increasing observation times in
     *                         years; the last is maturity
     * @param autocallBarrier  autocall trigger as a fraction of initial spot
     * @param couponBarrier    coupon trigger, <= autocallBarrier
     * @param knockInBarrier   protection barrier as a fraction of initial spot
     * @param couponPerPeriod  coupon per observation period as a fraction
     *                         of notional
     * @param memoryCoupons    missed coupons are caught up at the next
     *                         paying observation (Phoenix memory)
     */
    Autocallable(double notional, const std::vector<double>& observationYears,
                 double autocallBarrier, double couponBarrier,
                 double knockInBarrier, double couponPerPeriod,
                 bool memoryCoupons);

    /**
     * Monte Carlo present value under GBM. spot is the current underlier
     * level, initial the strike-setting S0 (equal at issue), vol the flat
     * Black-Scholes vol (pick it from the downside smile region, not ATM),
     * paths antithetic (2 per draw), fixed seed = reproducible price.
     */
    double price(double spot, double initial, double vol, double rate,
                 double divYield, int paths, std::int64_t seed) const;

    double notional() const;

    int observations() const;

private:
    /** One path's discounted payoff; sign flips the draws (antithetic). */
    double pathValue(double spot, double initial, const std::vector<double>& drift,
                     const std::vector<double>& volStep,
                     const std::vector<double>& discount,
                     const std::vector<double>& gaussians, int sign) const;

    double notional_;
    std::vector<double> observationYears_;
    double autocallBarrier_;  // fraction of S0
    double couponBarrier_;    // fraction of S0
    double knockInBarrier_;   // fraction of S0
    double couponPerPeriod_;  // fraction of notional
    bool memoryCoupons_;
};

}  // namespace quantfinlib
