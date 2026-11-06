// Ports of the Java rates curve instruments: Deposit, Fra, RateFuture, OisSwap
// and InterestRateSwap. Each states its pillar date, its market quote and the
// quote a pair of curves implies; multi_curve_bootstrap drives the difference
// to zero. Payment dates come from BusinessCalendar::schedule.
#pragma once

#include <memory>
#include <vector>

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/day_count.hpp"
#include "quantfinlib/rates/discount_curve.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class CurveInstrument {
public:
    virtual ~CurveInstrument() = default;
    virtual LocalDate pillarDate() const = 0;
    virtual double quote() const = 0;
    virtual double impliedQuote(const DiscountCurve& discount,
                                const DiscountCurve& projection) const = 0;
    /** True when the quote depends on the discount curve alone. */
    virtual bool discountOnly() const { return false; }
};

using CurveInstrumentPtr = std::shared_ptr<const CurveInstrument>;

/** A simple-rate period on the projection curve; a FRA quotes the same forward. */
class Deposit : public CurveInstrument {
public:
    Deposit(const LocalDate& start, const LocalDate& end, double rate, DayCount dayCount);
    LocalDate pillarDate() const override { return end_; }
    double quote() const override { return rate_; }
    double impliedQuote(const DiscountCurve& discount,
                        const DiscountCurve& projection) const override;

private:
    LocalDate start_, end_;
    double rate_;
    DayCount dayCount_;
};

using Fra = Deposit;

/** Rate future: forward plus Hull-White convexity. ofPrice takes 100 - rate. */
class RateFuture : public CurveInstrument {
public:
    RateFuture(const LocalDate& start, const LocalDate& end, double rate, DayCount dayCount,
               double meanReversion, double sigma);
    static RateFuture ofPrice(const LocalDate& start, const LocalDate& end, double price,
                              DayCount dayCount, double meanReversion, double sigma);
    /** Futures minus forward rate, continuously compounded, for 0 <= t1 < t2. */
    static double hullWhiteConvexity(double t1, double t2, double meanReversion, double sigma);
    LocalDate pillarDate() const override { return end_; }
    double quote() const override { return rate_; }
    double impliedQuote(const DiscountCurve& discount,
                        const DiscountCurve& projection) const override;

private:
    LocalDate start_, end_;
    double rate_;
    DayCount dayCount_;
    double meanReversion_, sigma_;
};

/** Compounded overnight swap: par = (DF(start) - DF(end)) / annuity on one curve. */
class OisSwap : public CurveInstrument {
public:
    OisSwap(const LocalDate& start, const LocalDate& end, double fixedRate, int paymentsPerYear,
            DayCount dayCount, const BusinessCalendar& calendar, BusinessCalendar::Roll roll);
    double annuity(const DiscountCurve& discount) const;
    double parRate(const DiscountCurve& discount) const;
    LocalDate pillarDate() const override { return pay_.back(); }
    double quote() const override { return fixedRate_; }
    double impliedQuote(const DiscountCurve& discount,
                        const DiscountCurve& projection) const override;
    bool discountOnly() const override { return true; }

private:
    LocalDate start_;
    double fixedRate_;
    DayCount dayCount_;
    std::vector<LocalDate> pay_;
};

/** Fixed against a term rate: float projected on one curve, discounted on another. */
class InterestRateSwap : public CurveInstrument {
public:
    InterestRateSwap(const LocalDate& start, const LocalDate& end, double fixedRate,
                     int fixedPerYear, DayCount fixedDayCount, int floatPerYear,
                     DayCount floatDayCount, const BusinessCalendar& calendar,
                     BusinessCalendar::Roll roll);
    double annuity(const DiscountCurve& discount) const;
    double floatLegPv(const DiscountCurve& discount, const DiscountCurve& projection) const;
    double parRate(const DiscountCurve& discount, const DiscountCurve& projection) const;
    double payerPv(const DiscountCurve& discount, const DiscountCurve& projection) const;
    LocalDate pillarDate() const override;
    double quote() const override { return fixedRate_; }
    double impliedQuote(const DiscountCurve& discount,
                        const DiscountCurve& projection) const override;

private:
    LocalDate start_;
    double fixedRate_;
    DayCount fixedDayCount_, floatDayCount_;
    std::vector<LocalDate> fixedPay_, floatPay_;
};

}  // namespace quantfinlib
