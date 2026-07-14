// Port of com.quantfinlib.fx.FxSwap (Java): two offsetting FX exchanges —
// buy (sell) base currency on the near date, sell (buy) it back on the far
// date. An at-market swap has zero value at inception: both legs trade at
// the prevailing outrights, so only the points differential between the
// legs is economically exchanged. Sign convention: baseNotional > 0 means
// the near leg BUYS base currency (far leg sells it back). A leg whose
// settlement date lies before the marking curve's spot has already settled
// (realized cash, not MTM) and contributes zero.
#pragma once

#include <string>

#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/fx/swap_points_curve.hpp"
#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

namespace quantfinlib {

class FxSwap {
public:
    /** Explicit legs (off-market swaps, historical bookings). */
    static FxSwap of(CurrencyPair pair, double baseNotional,
                     LocalDate nearDate, double nearRate,
                     LocalDate farDate, double farRate);

    /**
     * At-market swap struck off a points curve: both legs at the curve's
     * outrights, so inception value is zero by construction. "SPOT" is
     * accepted as the near tenor for the classic spot-against-forward swap.
     */
    static FxSwap atMarket(const SwapPointsCurve& curve, const std::string& nearTenor,
                           const std::string& farTenor, double baseNotional);

    /**
     * Undiscounted mark-to-market in quote currency against a current curve.
     * Aged swaps: a settled leg contributes zero (see class doc).
     */
    double markToMarket(const SwapPointsCurve& current) const;

    /**
     * Discounted mark-to-market: live-leg P&Ls discounted off a
     * quote-currency zero curve (ACT/365 from the valuation curve's spot).
     */
    double markToMarket(const SwapPointsCurve& current, const YieldCurve& quoteDiscount) const;

    /** The swap's traded points differential (far - near) in pips. */
    double swapPointsPips() const { return pair_.pips(farRate_ - nearRate_); }

    /**
     * Cost in quote currency of rolling a base position one day at a
     * quoted tom-next rate.
     */
    static double rollCost(const CurrencyPair& pair, double baseNotional, double tomNextPips);

    const CurrencyPair& pair() const { return pair_; }
    double baseNotional() const { return baseNotional_; }
    const LocalDate& nearDate() const { return nearDate_; }
    double nearRate() const { return nearRate_; }
    const LocalDate& farDate() const { return farDate_; }
    double farRate() const { return farRate_; }

    std::string toString() const;

private:
    FxSwap(CurrencyPair pair, double baseNotional, LocalDate nearDate, double nearRate,
           LocalDate farDate, double farRate);

    static LocalDate dateOf(const SwapPointsCurve& curve, const std::string& tenor);

    /** One leg's undiscounted P&L: zero once settled, spot at spot, else the outright. */
    double legPnl(const LocalDate& legDate, double legRate, int sign,
                 const SwapPointsCurve& current) const;

    CurrencyPair pair_;
    double baseNotional_;
    LocalDate nearDate_;
    double nearRate_;
    LocalDate farDate_;
    double farRate_;
};

}  // namespace quantfinlib
