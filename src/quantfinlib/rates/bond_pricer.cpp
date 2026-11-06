#include "quantfinlib/rates/bond_pricer.hpp"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace bond_pricer {

namespace {

// Coupon grid: n coupons, the first w of a period away (0 < w <= 1).
struct Grid {
    int n;
    double w;
};

Grid grid(int frequency, double yearsToMaturity) {
    if (frequency < 1 || !(yearsToMaturity > 0) || !std::isfinite(yearsToMaturity)) {
        throw std::invalid_argument("need positive frequency and maturity");
    }
    double x = yearsToMaturity * frequency;
    double whole = std::nearbyint(x);
    if (std::fabs(x - whole) <= 1e-9 && whole >= 1) {
        return {static_cast<int>(whole), 1.0};
    }
    double n = std::ceil(x);
    return {static_cast<int>(n), x - (n - 1)};
}

// A settlement date's place on the coupon schedule.
struct Position {
    LocalDate prev;
    LocalDate next;
    int remaining;
    int months;
};

Position locate(const LocalDate& settlement, const LocalDate& maturity, int frequency,
                bool endOfMonth) {
    if (frequency < 1 || 12 % frequency != 0) {
        throw std::invalid_argument("frequency must divide 12");
    }
    if (!maturity.isAfter(settlement)) {
        throw std::invalid_argument("maturity must be after settlement");
    }
    int months = 12 / frequency;
    int k = 0;
    LocalDate next = maturity;
    while (true) {
        LocalDate before = BusinessCalendar::shiftMonths(
                maturity, -static_cast<std::int64_t>(months) * (k + 1), endOfMonth);
        if (!before.isAfter(settlement)) {
            return {before, next, k + 1, months};
        }
        next = before;
        k++;
    }
}

double periodCoupon(double face, double couponRate, int frequency, const LocalDate& start,
                    const LocalDate& end, DayCount dayCount) {
    switch (dayCount) {
        case DayCount::THIRTY_360:
        case DayCount::THIRTY_E_360:
        case DayCount::THIRTY_E_360_ISDA:
        case DayCount::ACT_ACT_ICMA:
            return face * couponRate / frequency;
        default:
            return face * couponRate * day_count::yearFraction(dayCount, start, end);
    }
}

// Share of the current coupon period elapsed at settlement.
double elapsed(const Position& p, const LocalDate& settlement, int frequency, DayCount dayCount,
               const LocalDate& maturity) {
    switch (dayCount) {
        case DayCount::ACT_ACT_ICMA:
            return day_count::actActIcma(p.prev, settlement, p.prev, p.next, frequency)
                    * frequency;
        case DayCount::THIRTY_360:
        case DayCount::THIRTY_E_360:
        case DayCount::THIRTY_E_360_ISDA:
            return day_count::yearFraction(dayCount, p.prev, settlement, maturity) * frequency;
        default:
            return day_count::yearFraction(dayCount, p.prev, settlement)
                    / day_count::yearFraction(dayCount, p.prev, p.next);
    }
}

}  // namespace

double priceFromYield(double face, double couponRate, int frequency,
                      double yearsToMaturity, double yield) {
    Grid g = grid(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double y = yield / frequency;
    double price = 0;
    for (int k = 1; k <= g.n; k++) {
        double cf = coupon + (k == g.n ? face : 0);
        price += cf / std::pow(1 + y, k - 1 + g.w);
    }
    return price;
}

double priceFromCurve(double face, double couponRate, int frequency,
                      double yearsToMaturity, const YieldCurve& curve) {
    Grid g = grid(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double price = 0;
    for (int k = 1; k <= g.n; k++) {
        double cf = coupon + (k == g.n ? face : 0);
        price += cf * curve.discountFactor((k - 1 + g.w) / frequency);
    }
    return price;
}

double yieldToMaturity(double price, double face, double couponRate,
                       int frequency, double yearsToMaturity) {
    double lo = -0.9, hi = 10;
    // Bracket check: a price outside [PV(10), PV(-0.9)] has no yield in
    // the search range, and bisection would silently converge to an
    // endpoint and hand back -89.99% or 1000% as if it were a market
    // yield. Refuse loudly instead.
    double maxPrice = priceFromYield(face, couponRate, frequency, yearsToMaturity, lo);
    double minPrice = priceFromYield(face, couponRate, frequency, yearsToMaturity, hi);
    if (!(price >= minPrice) || !(price <= maxPrice)) {
        throw std::invalid_argument("price " + std::to_string(price)
                + " has no yield in [-90%, 1000%] (attainable price range ["
                + std::to_string(minPrice) + ", " + std::to_string(maxPrice) + "])");
    }
    for (int i = 0; i < 200; i++) {
        double mid = (lo + hi) / 2;
        if (priceFromYield(face, couponRate, frequency, yearsToMaturity, mid) > price) {
            lo = mid;   // price too high -> yield higher
        } else {
            hi = mid;
        }
    }
    return (lo + hi) / 2;
}

double macaulayDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield) {
    Grid g = grid(frequency, yearsToMaturity);
    double coupon = face * couponRate / frequency;
    double y = yield / frequency;
    double weighted = 0, price = 0;
    for (int k = 1; k <= g.n; k++) {
        double e = k - 1 + g.w;
        double cf = coupon + (k == g.n ? face : 0);
        double pv = cf / std::pow(1 + y, e);
        weighted += e / frequency * pv;
        price += pv;
    }
    return weighted / price;
}

double modifiedDuration(double face, double couponRate, int frequency,
                        double yearsToMaturity, double yield) {
    return macaulayDuration(face, couponRate, frequency, yearsToMaturity, yield)
            / (1 + yield / frequency);
}

double convexity(double face, double couponRate, int frequency,
                 double yearsToMaturity, double yield) {
    double h = 1e-4;
    double p0 = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield);
    double up = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield + h);
    double dn = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield - h);
    return (up + dn - 2 * p0) / (p0 * h * h);
}

double dv01(double face, double couponRate, int frequency,
            double yearsToMaturity, double yield) {
    double price = priceFromYield(face, couponRate, frequency, yearsToMaturity, yield);
    return modifiedDuration(face, couponRate, frequency, yearsToMaturity, yield)
            * price * 1e-4;
}

double accruedInterest(double face, double couponRate, int frequency, double yearsToMaturity) {
    return face * couponRate / frequency * (1 - grid(frequency, yearsToMaturity).w);
}

double dirtyPrice(double face, double couponRate, int frequency, const LocalDate& settlement,
                  const LocalDate& maturity, double yield, DayCount dayCount, bool endOfMonth) {
    Position p = locate(settlement, maturity, frequency, endOfMonth);
    double w = 1 - elapsed(p, settlement, frequency, dayCount, maturity);
    double v = 1 / (1 + yield / frequency);
    double price = 0;
    LocalDate start = p.prev;
    for (int k = 0; k < p.remaining; k++) {
        LocalDate end = BusinessCalendar::shiftMonths(
                maturity, -static_cast<std::int64_t>(p.months) * (p.remaining - 1 - k),
                endOfMonth);
        double cf = periodCoupon(face, couponRate, frequency, start, end, dayCount);
        if (k == p.remaining - 1) {
            cf += face;
        }
        price += cf * std::pow(v, k + w);
        start = end;
    }
    return price;
}

double accruedInterest(double face, double couponRate, int frequency,
                       const LocalDate& settlement, const LocalDate& maturity,
                       DayCount dayCount, bool endOfMonth) {
    Position p = locate(settlement, maturity, frequency, endOfMonth);
    return periodCoupon(face, couponRate, frequency, p.prev, p.next, dayCount)
            * elapsed(p, settlement, frequency, dayCount, maturity);
}

double cleanPrice(double face, double couponRate, int frequency, const LocalDate& settlement,
                  const LocalDate& maturity, double yield, DayCount dayCount, bool endOfMonth) {
    return dirtyPrice(face, couponRate, frequency, settlement, maturity, yield, dayCount,
                      endOfMonth)
            - accruedInterest(face, couponRate, frequency, settlement, maturity, dayCount,
                              endOfMonth);
}

double yieldFromCleanPrice(double cleanPriceValue, double face, double couponRate, int frequency,
                           const LocalDate& settlement, const LocalDate& maturity,
                           DayCount dayCount, bool endOfMonth) {
    double lo = -0.9, hi = 10;
    auto clean = [&](double y) {
        return cleanPrice(face, couponRate, frequency, settlement, maturity, y, dayCount,
                          endOfMonth);
    };
    if (!(cleanPriceValue <= clean(lo)) || !(cleanPriceValue >= clean(hi))) {
        throw std::invalid_argument("price " + std::to_string(cleanPriceValue)
                                    + " has no yield in [-90%, 1000%]");
    }
    for (int i = 0; i < 200 && hi - lo > 1e-16; i++) {
        double mid = 0.5 * (lo + hi);
        if (clean(mid) > cleanPriceValue) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

LocalDate settlementDate(const LocalDate& tradeDate, int lagDays,
                         const BusinessCalendar& calendar) {
    return calendar.addBusinessDays(tradeDate, lagDays);
}

}  // namespace bond_pricer
}  // namespace quantfinlib
