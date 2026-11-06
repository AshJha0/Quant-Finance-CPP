// Port of com.quantfinlib.rates.RatesOptions (Java): RATES VOLATILITY
// products priced off the curve.
//   Swaption — an option on a forward-starting swap: the underlying is
//   the forward swap rate F = (DF(start) - DF(end)) / annuity, the
//   natural numeraire is the ANNUITY, and the price is simply
//   annuity * Black76(F, K, vol, expiry) — discounting lives entirely in
//   the annuity, which is why the Black-76 leg is called with rate 0.
//   A PAYER swaption (right to pay fixed) is a CALL on the swap rate.
//   Cap / floor — a strip of independent Black-76 caplets (floorlets),
//   one per accrual period, on that period's SIMPLE forward rate
//   f_i = (DF(t_{i-1})/DF(t_i) - 1)/tau, fixing at period START, paying
//   at its end. The first period's rate is already fixed today, so its
//   caplet is pure intrinsic — included (market convention).
// Two identities keep this honest (both pinned by tests): payer -
// receiver = annuity*(F - K), and cap - floor = the matching swap's PV.
// The int overloads are annual and lognormal, and throw when a forward is
// not positive. The VolModel overloads take any payment frequency and a
// normal (Bachelier) or shifted lognormal model, which is how negative rates
// are priced. One flat vol per product.
#pragma once

#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {
namespace rates_options {

/** PV of 1 per year paid annually over (startYears, startYears+tenorYears]. */
double annuity(const YieldCurve& curve, int startYears, int tenorYears);

/** Forward par swap rate for a swap starting at `startYears`. */
double forwardSwapRate(const YieldCurve& curve, int startYears, int tenorYears);

/**
 * Black-76 swaption price per unit notional.
 * payer: true = payer swaption (call on the swap rate);
 * vol: flat lognormal vol of the forward swap rate.
 */
double swaption(const YieldCurve& curve, int startYears, int tenorYears,
                double strike, double vol, bool payer);

/** The vol quoting convention of an option on a rate. */
class VolModel {
public:
    enum class Type { LOGNORMAL, SHIFTED_LOGNORMAL, NORMAL };

    static VolModel lognormal() { return VolModel(Type::LOGNORMAL, 0); }
    /** Black on F + shift, K + shift; the market's displaced diffusion. */
    static VolModel shiftedLognormal(double shift);
    static VolModel normal() { return VolModel(Type::NORMAL, 0); }

    Type type() const { return type_; }
    double shift() const { return shift_; }

    /**
     * Undiscounted option value. An expired option is intrinsic in every
     * model. Lognormal models throw unless F + shift > 0 and K + shift > 0.
     */
    double forwardValue(OptionType type, double forward, double strike, double vol,
                        double expiry) const;

    /** Vol that reproduces an undiscounted value; NaN when none exists. */
    double impliedVol(OptionType type, double forwardValue, double forward, double strike,
                      double expiry) const;

private:
    VolModel(Type type, double shift) : type_(type), shift_(shift) {}

    Type type_;
    double shift_;
};

/** Annuity sum tau*DF over periods of 1/paymentsPerYear after startYears. */
double annuity(const YieldCurve& curve, double startYears, double tenorYears,
               int paymentsPerYear);

double forwardSwapRate(const YieldCurve& curve, double startYears, double tenorYears,
                       int paymentsPerYear);

/** Swaption under a vol model; strike may be negative in normal and shifted models. */
double swaption(const YieldCurve& curve, double startYears, double tenorYears,
                int paymentsPerYear, double strike, double vol, bool payer,
                const VolModel& model);

/** The vol that reprices a swaption under a vol model. */
double swaptionImpliedVol(const YieldCurve& curve, double startYears, double tenorYears,
                          int paymentsPerYear, double strike, double price, bool payer,
                          const VolModel& model);

/** Cap (isCap) or floor under a vol model; tau-weighted caplets, first fixed at 0. */
double capFloor(const YieldCurve& curve, double maturityYears, int paymentsPerYear,
                double strike, double vol, bool isCap, const VolModel& model);

/** Cap: strip of annual Black-76 caplets to `maturityYears`. */
double cap(const YieldCurve& curve, int maturityYears, double strike, double vol);

/** Floor: the matching strip of floorlets. */
double floor(const YieldCurve& curve, int maturityYears, double strike, double vol);

}  // namespace rates_options
}  // namespace quantfinlib
