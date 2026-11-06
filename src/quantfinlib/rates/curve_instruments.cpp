#include "quantfinlib/rates/curve_instruments.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace quantfinlib {

namespace {

void requirePeriod(const LocalDate& start, const LocalDate& end) {
    if (!end.isAfter(start)) {
        throw std::invalid_argument("end must be after start: " + start.toString() + " "
                                    + end.toString());
    }
}

void requireFinite(double v) {
    if (!std::isfinite(v)) {
        throw std::invalid_argument("quote must be finite");
    }
}

double legAnnuity(const DiscountCurve& d, const LocalDate& start,
                  const std::vector<LocalDate>& pay, DayCount dc) {
    double a = 0;
    LocalDate prev = start;
    for (const LocalDate& p : pay) {
        a += day_count::yearFraction(dc, prev, p) * d.discountFactor(p);
        prev = p;
    }
    return a;
}

}  // namespace

Deposit::Deposit(const LocalDate& start, const LocalDate& end, double rate, DayCount dayCount)
    : start_(start), end_(end), rate_(rate), dayCount_(dayCount) {
    requirePeriod(start, end);
    requireFinite(rate);
}

double Deposit::impliedQuote(const DiscountCurve&, const DiscountCurve& projection) const {
    return projection.forwardRate(start_, end_, dayCount_);
}

RateFuture::RateFuture(const LocalDate& start, const LocalDate& end, double rate,
                       DayCount dayCount, double meanReversion, double sigma)
    : start_(start), end_(end), rate_(rate), dayCount_(dayCount),
      meanReversion_(meanReversion), sigma_(sigma) {
    requirePeriod(start, end);
    requireFinite(rate);
    if (!(meanReversion >= 0) || !std::isfinite(meanReversion) || !(sigma >= 0)
            || !std::isfinite(sigma)) {
        throw std::invalid_argument("meanReversion and sigma must be finite and >= 0");
    }
}

RateFuture RateFuture::ofPrice(const LocalDate& start, const LocalDate& end, double price,
                               DayCount dayCount, double meanReversion, double sigma) {
    return RateFuture(start, end, (100 - price) / 100, dayCount, meanReversion, sigma);
}

double RateFuture::hullWhiteConvexity(double t1, double t2, double a, double sigma) {
    if (!(t1 >= 0) || !(t2 > t1) || !std::isfinite(t2)) {
        throw std::invalid_argument("need 0 <= t1 < t2");
    }
    double s2 = sigma * sigma;
    if (a == 0) {
        return 0.5 * s2 * t1 * t2;
    }
    double b12 = -std::expm1(-a * (t2 - t1)) / a;
    double b01 = -std::expm1(-a * t1) / a;
    return b12 / (t2 - t1) * (b12 * -std::expm1(-2 * a * t1) + 2 * a * b01 * b01) * s2 / (4 * a);
}

double RateFuture::impliedQuote(const DiscountCurve&, const DiscountCurve& projection) const {
    double t1 = projection.time(start_);
    double t2 = projection.time(end_);
    double forwardCc = (projection.minusLogDf(t2) - projection.minusLogDf(t1)) / (t2 - t1);
    double futuresCc = forwardCc
            + hullWhiteConvexity(std::max(0.0, t1), t2, meanReversion_, sigma_);
    return std::expm1(futuresCc * (t2 - t1)) / day_count::yearFraction(dayCount_, start_, end_);
}

OisSwap::OisSwap(const LocalDate& start, const LocalDate& end, double fixedRate,
                 int paymentsPerYear, DayCount dayCount, const BusinessCalendar& calendar,
                 BusinessCalendar::Roll roll)
    : start_(start), fixedRate_(fixedRate), dayCount_(dayCount),
      pay_(calendar.schedule(start, end, paymentsPerYear, roll)) {
    requireFinite(fixedRate);
}

double OisSwap::annuity(const DiscountCurve& discount) const {
    return legAnnuity(discount, start_, pay_, dayCount_);
}

double OisSwap::parRate(const DiscountCurve& discount) const {
    return (discount.discountFactor(start_) - discount.discountFactor(pay_.back()))
            / annuity(discount);
}

double OisSwap::impliedQuote(const DiscountCurve& discount, const DiscountCurve&) const {
    return parRate(discount);
}

InterestRateSwap::InterestRateSwap(const LocalDate& start, const LocalDate& end, double fixedRate,
                                   int fixedPerYear, DayCount fixedDayCount, int floatPerYear,
                                   DayCount floatDayCount, const BusinessCalendar& calendar,
                                   BusinessCalendar::Roll roll)
    : start_(start), fixedRate_(fixedRate), fixedDayCount_(fixedDayCount),
      floatDayCount_(floatDayCount), fixedPay_(calendar.schedule(start, end, fixedPerYear, roll)),
      floatPay_(calendar.schedule(start, end, floatPerYear, roll)) {
    requireFinite(fixedRate);
}

double InterestRateSwap::annuity(const DiscountCurve& discount) const {
    return legAnnuity(discount, start_, fixedPay_, fixedDayCount_);
}

double InterestRateSwap::floatLegPv(const DiscountCurve& discount,
                                    const DiscountCurve& projection) const {
    double pv = 0;
    LocalDate prev = start_;
    for (const LocalDate& d : floatPay_) {
        pv += day_count::yearFraction(floatDayCount_, prev, d)
                * projection.forwardRate(prev, d, floatDayCount_) * discount.discountFactor(d);
        prev = d;
    }
    return pv;
}

double InterestRateSwap::parRate(const DiscountCurve& discount,
                                 const DiscountCurve& projection) const {
    return floatLegPv(discount, projection) / annuity(discount);
}

double InterestRateSwap::payerPv(const DiscountCurve& discount,
                                 const DiscountCurve& projection) const {
    return floatLegPv(discount, projection) - fixedRate_ * annuity(discount);
}

LocalDate InterestRateSwap::pillarDate() const {
    return std::max(fixedPay_.back(), floatPay_.back());
}

double InterestRateSwap::impliedQuote(const DiscountCurve& discount,
                                      const DiscountCurve& projection) const {
    return parRate(discount, projection);
}

}  // namespace quantfinlib
