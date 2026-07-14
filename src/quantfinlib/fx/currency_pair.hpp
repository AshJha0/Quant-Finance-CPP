// Port of com.quantfinlib.fx.CurrencyPair (Java): market conventions for an
// FX pair — quotation precision, pip size, spot lag, and settlement-date
// arithmetic against BOTH currencies' holiday calendars (holiday union as
// one joint calendar). JPY-quoted pairs quote to 3 decimals (pip 0.01),
// everything else 5 (pip 0.0001); spot is T+2 with the market's T+1
// exceptions (USDCAD/USDTRY/USDRUB/USDPHP). Month/year tenors roll
// modified-following with the end-end rule. As in the Java class,
// intermediate days of the spot lag are counted on the joint calendar — a
// slight simplification of the full market rule (where a USD holiday only
// blocks the settlement day itself).
#pragma once

#include <string>

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class CurrencyPair {
public:
    /** Standard conventions for a 6-letter pair code ("EURUSD"), weekends-only. */
    static CurrencyPair of(const std::string& pair);

    /** Standard conventions for an explicit base/quote, weekends-only calendars. */
    static CurrencyPair of(const std::string& base, const std::string& quote);

    /** Fully custom conventions (exotic pairs, onshore fixings, tests). */
    static CurrencyPair custom(const std::string& base, const std::string& quote,
                               double pipSize, int pricePrecision, int spotLagDays,
                               BusinessCalendar baseCalendar, BusinessCalendar quoteCalendar);

    /** Same conventions with real holiday calendars for each currency's center. */
    CurrencyPair withCalendars(BusinessCalendar baseCalendar,
                               BusinessCalendar quoteCalendar) const;

    const std::string& base() const { return base_; }
    const std::string& quote() const { return quote_; }

    /** "EURUSD" style symbol, the natural key for the tick bus. */
    std::string symbol() const { return base_ + quote_; }

    /** One pip in price terms (0.0001, or 0.01 for JPY quotes). */
    double pipSize() const { return pipSize_; }

    /** Quoted decimal places (5, or 3 for JPY quotes). */
    int pricePrecision() const { return pricePrecision_; }

    /** Spot settlement lag in business days (T+2, or T+1 exceptions). */
    int spotLagDays() const { return spotLagDays_; }

    /** Converts a price difference to pips (e.g. 0.00013 -> 1.3 pips on EURUSD). */
    double pips(double priceDifference) const { return priceDifference / pipSize_; }

    /** Converts pips to a price difference (the inverse of pips()). */
    double priceFromPips(double p) const { return p * pipSize_; }

    /** Rounds a raw price to the pair's quoted precision (half-up). */
    double round(double price) const;

    /** The base currency's own holiday calendar (single-center conventions). */
    const BusinessCalendar& baseCalendar() const { return baseCalendar_; }

    /** The quote currency's own calendar — what NDF fixing conventions count in. */
    const BusinessCalendar& quoteCalendar() const { return quoteCalendar_; }

    /** Business day in BOTH currencies' calendars. */
    bool isJointBusinessDay(const LocalDate& date) const {
        return jointCalendar_.isBusinessDay(date);
    }

    /** Spot settlement date: spotLagDays joint business days after the trade date. */
    LocalDate spotDate(const LocalDate& tradeDate) const {
        return addJointBusinessDays(tradeDate, spotLagDays_);
    }

    /** Inverse of spotDate: the trade date whose spot is spotDate. */
    LocalDate tradeDateForSpot(const LocalDate& spot) const {
        return jointCalendar_.subtractBusinessDays(spot, spotLagDays_);
    }

    /**
     * Forward settlement date for a market tenor, measured from the pair's
     * spot date. Supported: ON, TN, SN, <n>D, <n>W (rolled following),
     * <n>M, <n>Y (modified-following with the end-end rule).
     */
    LocalDate tenorDate(const LocalDate& tradeDate, const std::string& tenor) const;

    /** Adds n joint business days (n >= 0). */
    LocalDate addJointBusinessDays(const LocalDate& date, int n) const {
        return jointCalendar_.addBusinessDays(date, n);
    }

    std::string toString() const { return symbol(); }

private:
    CurrencyPair(std::string base, std::string quote, double pipSize, int pricePrecision,
                 int spotLagDays, BusinessCalendar baseCalendar, BusinessCalendar quoteCalendar);

    static int spotLagFor(const std::string& base, const std::string& quote);

    LocalDate monthTenor(const LocalDate& spot, int months) const;
    LocalDate lastJointBusinessDayOfMonth(const LocalDate& anyDayInMonth) const;

    std::string base_;
    std::string quote_;
    double pipSize_;
    int pricePrecision_;
    int spotLagDays_;
    BusinessCalendar baseCalendar_;
    BusinessCalendar quoteCalendar_;
    BusinessCalendar jointCalendar_;
};

}  // namespace quantfinlib
