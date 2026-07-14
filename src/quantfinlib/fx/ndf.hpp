// Port of com.quantfinlib.fx.Ndf (Java): a non-deliverable forward — the FX
// forward for restricted currencies (INR, KRW, TWD, BRL, CNY, ...) where the
// local currency never moves; the trade cash-settles in the deliverable
// (usually USD, base) currency against an official fixing published before
// settlement. Fixing date precedes settlement by the restricted currency's
// fixing lag (typically 2 local business days, 1 for BRL/PTAX). Settlement
// to the BUYER of base currency, paid in base currency:
//   amount = baseNotional * (fixing - contractRate) / fixing
#pragma once

#include <string>

#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/fx/swap_points_curve.hpp"
#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {

class Ndf {
public:
    /**
     * Books an NDF at a market tenor: settlement from the pair's tenor
     * arithmetic, fixing walked back by the restricted (quote) currency's
     * lag counted in LOCAL (quote-calendar) business days. Pass real
     * holiday calendars via CurrencyPair::withCalendars for production dates.
     */
    static Ndf of(const CurrencyPair& pair, const LocalDate& tradeDate, const std::string& tenor,
                  double contractRate, double baseNotional);

    /** Explicit dates (broken dates, historical bookings). */
    static Ndf of(CurrencyPair pair, double baseNotional, double contractRate,
                  LocalDate fixingDate, LocalDate settlementDate);

    /** The fixing lag this library books for a restricted currency code. */
    static int fixingLagDays(const std::string& currency);

    /**
     * Cash settlement in base (deliverable) currency once the official
     * fixing publishes. Positive pays the base buyer.
     */
    double settlementAmount(double fixingRate) const;

    /**
     * Undiscounted mark-to-market in base currency: the settlement formula
     * evaluated at the curve's forward to the FIXING DATE. An NDF inside
     * its fixing window (fixing date at or before the curve's spot date)
     * degrades to the spot outright instead of throwing mid-lifecycle.
     */
    double markToMarket(const SwapPointsCurve& current) const;

    /**
     * Discounted mark-to-market: the expected settlement discounted from
     * the settlement date on a base-currency (USD) zero curve, ACT/365
     * from the curve's spot.
     */
    double markToMarket(const SwapPointsCurve& current, const YieldCurve& baseDiscount) const;

    const CurrencyPair& pair() const { return pair_; }
    double baseNotional() const { return baseNotional_; }
    double contractRate() const { return contractRate_; }
    const LocalDate& fixingDate() const { return fixingDate_; }
    const LocalDate& settlementDate() const { return settlementDate_; }

    std::string toString() const;

private:
    Ndf(CurrencyPair pair, double baseNotional, double contractRate,
        LocalDate fixingDate, LocalDate settlementDate);

    CurrencyPair pair_;
    double baseNotional_;
    double contractRate_;
    LocalDate fixingDate_;
    LocalDate settlementDate_;
};

}  // namespace quantfinlib
