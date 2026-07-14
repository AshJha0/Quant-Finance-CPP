// Port of com.quantfinlib.fx.SwapPointsCurve (Java): the market's quoted
// forward points per tenor, turned into outright forwards for any
// settlement date. Points interpolate linearly in ACTUAL DAYS between
// pillars (the interbank convention for broken dates), extrapolate the
// final segment's slope beyond the last pillar, and interpolate from zero
// points at spot before the first. impliedCarry inverts covered interest
// parity: ln(F/S)/tau, ACT/365. Immutable after Builder::build; lookups
// are allocation-free (binary search over primitive arrays).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class SwapPointsCurve {
public:
    /** Accumulates tenor/points quotes, then freezes them into a curve. */
    class Builder {
    public:
        Builder(CurrencyPair pair, LocalDate tradeDate, double spotRate);

        /**
         * Adds a pillar quoted in PIPS (market form: "1M EURUSD +12.6"),
         * scaled by the pair's pip size internally. Negative points are
         * normal when the base currency yields more than the quote currency.
         */
        Builder& add(const std::string& tenor, double pips);

        /** Throws std::runtime_error (Java IllegalStateException) when empty. */
        SwapPointsCurve build() const;

    private:
        CurrencyPair pair_;
        LocalDate tradeDate_;
        double spotRate_;
        std::vector<std::string> tenors_;
        std::vector<LocalDate> dates_;
        std::vector<double> points_;
    };

    static Builder builder(CurrencyPair pair, LocalDate tradeDate, double spotRate) {
        return Builder(std::move(pair), tradeDate, spotRate);
    }

    /**
     * Interpolated forward points (price terms) for a settlement date:
     * linear in actual days, anchored at zero on the spot date.
     */
    double forwardPoints(const LocalDate& valueDate) const;

    /** Outright forward: spot plus interpolated points. */
    double outright(const LocalDate& valueDate) const {
        return spotRate_ + forwardPoints(valueDate);
    }

    /** Outright forward for a market tenor of this curve's pair. */
    double outright(const std::string& tenor) const {
        return outright(tenorDateFromSpot(tenor));
    }

    /**
     * Continuously compounded rate differential (quote minus base) implied
     * by covered interest parity at a date: ln(F/S)/tau, ACT/365.
     */
    double impliedCarry(const LocalDate& valueDate) const;

    /** The spot settlement date all pillar offsets are measured from. */
    const LocalDate& spotDate() const { return spotDate_; }

    double spotRate() const { return spotRate_; }

    const CurrencyPair& pair() const { return pair_; }

    /** Pillar tenors in date order (diagnostics/reporting). */
    std::vector<std::string> pillarTenors() const { return tenors_; }

private:
    SwapPointsCurve(CurrencyPair pair, LocalDate spotDate, double spotRate,
                    std::vector<std::int64_t> pillarDays, std::vector<double> pillarPoints,
                    std::vector<std::string> tenors);

    /** Resolves a tenor to a date using this curve's spot as the anchor. */
    LocalDate tenorDateFromSpot(const std::string& tenor) const {
        return pair_.tenorDate(pair_.tradeDateForSpot(spotDate_), tenor);
    }

    CurrencyPair pair_;
    LocalDate spotDate_;
    double spotRate_;
    std::vector<std::int64_t> pillarDays_;  // offsets from spot, ascending
    std::vector<double> pillarPoints_;      // price terms (pips already scaled)
    std::vector<std::string> tenors_;
};

}  // namespace quantfinlib
