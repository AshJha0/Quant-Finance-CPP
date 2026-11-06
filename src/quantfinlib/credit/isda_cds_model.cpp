#include "quantfinlib/credit/isda_cds_model.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kSmall = 1e-6;
using Vec = std::vector<double>;

void validateTimes(const Vec& times, const Vec& values) {
    if (times.empty() || times.size() != values.size()) {
        throw std::invalid_argument("need aligned, non-empty times and values");
    }
    double prev = 0;
    for (double t : times) {
        if (!(t > prev) || t == kInf) {
            throw std::invalid_argument("times must be strictly ascending, positive and finite");
        }
        prev = t;
    }
}

double years(const LocalDate& base, const LocalDate& d) {
    return static_cast<double>(LocalDate::daysBetween(base, d)) / 365.0;
}

struct Seg {
    double lambda, k, dt, q0p0;
};

Seg segment(const IsdaCurve& discount, const IsdaCurve& survival, double a, double b) {
    double dt = b - a;
    double lambda = (survival.rateTimesTime(b) - survival.rateTimesTime(a)) / dt;
    double f = (discount.rateTimesTime(b) - discount.rateTimesTime(a)) / dt;
    return {lambda, lambda + f, dt, survival.value(a) * discount.value(a)};
}

// Integral of e^{-k s} on [0, dt].
double e1(double k, double dt) {
    double x = k * dt;
    if (std::fabs(x) < kSmall) {
        return dt * (1 - x / 2 + x * x / 6 - x * x * x / 24);
    }
    return -std::expm1(-x) / k;
}

// Integral of s e^{-k s} on [0, dt].
double e2(double k, double dt) {
    double x = k * dt;
    if (std::fabs(x) < 0.1) {
        // sum_n (-1)^n (n+1) x^n / (n+2)!, truncated after n = 12.
        double sum = 0;
        double term = 0.5;
        for (int n = 0; n <= 12; n++) {
            sum += (n + 1) * term;
            term *= -x / (n + 3);
        }
        return dt * dt * sum;
    }
    return (-std::expm1(-x) - x * std::exp(-x)) / (k * k);
}

Vec mergedGrid(const IsdaCurve& a, const IsdaCurve& b, double from, double to) {
    if (!(to > from)) {
        return {from};
    }
    Vec g = {from, to};
    for (const IsdaCurve* c : {&a, &b}) {
        for (double t : c->times()) {
            if (t > from && t < to) {
                g.push_back(t);
            }
        }
    }
    std::sort(g.begin(), g.end());
    g.erase(std::unique(g.begin(), g.end()), g.end());
    return g;
}

double solveFlatHazard(const std::function<double(double)>& f) {
    double lo = 0, hi = 50;
    if (f(lo) > 0 || f(hi) < 0) {
        throw std::invalid_argument("quote has no hazard in [0, 50]");
    }
    for (int it = 0; it < 200 && hi - lo > 1e-15; it++) {
        double mid = 0.5 * (lo + hi);
        if (f(mid) > 0) {
            hi = mid;
        } else {
            lo = mid;
        }
    }
    return 0.5 * (lo + hi);
}

}  // namespace

IsdaCurve IsdaCurve::ofZeroRates(const Vec& times, const Vec& zeroRates) {
    validateTimes(times, zeroRates);
    Vec rt(times.size());
    for (std::size_t i = 0; i < rt.size(); i++) {
        if (!std::isfinite(zeroRates[i])) {
            throw std::invalid_argument("zero rate must be finite");
        }
        rt[i] = zeroRates[i] * times[i];
    }
    return IsdaCurve(times, rt);
}

IsdaCurve IsdaCurve::ofHazardRates(const Vec& times, const Vec& hazards) {
    validateTimes(times, hazards);
    Vec t = times;
    Vec rt(t.size());
    double acc = 0, prev = 0;
    for (std::size_t i = 0; i < t.size(); i++) {
        if (!(hazards[i] >= 0) || hazards[i] == kInf) {
            throw std::invalid_argument("hazard must be >= 0 and finite");
        }
        acc += hazards[i] * (t[i] - prev);
        rt[i] = acc;
        prev = t[i];
    }
    // A far node keeps the last hazard in force; zero-rate extrapolation
    // would otherwise switch to the average hazard.
    t.push_back(t.back() + 1000);
    rt.push_back(acc + hazards.back() * 1000);
    return IsdaCurve(t, rt);
}

IsdaCurve IsdaCurve::flat(double rate) {
    return ofZeroRates({1}, {rate});
}

double IsdaCurve::rateTimesTime(double t) const {
    if (!(t >= 0) || t == kInf) {
        throw std::invalid_argument("t must be >= 0 and finite");
    }
    if (t <= times_.front()) {
        return rt_.front() / times_.front() * t;
    }
    if (t >= times_.back()) {
        return rt_.back() / times_.back() * t;
    }
    auto it = std::lower_bound(times_.begin(), times_.end(), t);
    std::size_t hi = static_cast<std::size_t>(it - times_.begin());
    if (times_[hi] == t) {
        return rt_[hi];
    }
    std::size_t lo = hi - 1;
    double w = (t - times_[lo]) / (times_[hi] - times_[lo]);
    return rt_[lo] + w * (rt_[hi] - rt_[lo]);
}

double IsdaCurve::zeroRate(double t) const {
    if (!(t > 0)) {
        throw std::invalid_argument("t must be > 0");
    }
    return rateTimesTime(t) / t;
}

double IsdaCurve::value(double t) const {
    return std::exp(-rateTimesTime(t));
}

namespace isda_cds {

Contract makeContract(const LocalDate& tradeDate, const LocalDate& stepIn,
                      const LocalDate& cashSettle, const LocalDate& accrualStart,
                      const LocalDate& maturity, double coupon, double recovery) {
    if (stepIn.isBefore(tradeDate) || cashSettle.isBefore(tradeDate)) {
        throw std::invalid_argument("step-in and cash settle must not precede trade");
    }
    if (accrualStart.isAfter(stepIn)) {
        throw std::invalid_argument("accrual start must be on or before step-in");
    }
    if (!maturity.isAfter(stepIn)) {
        throw std::invalid_argument("maturity must be after step-in");
    }
    if (!(coupon >= 0) || coupon == kInf) {
        throw std::invalid_argument("coupon must be >= 0 and finite");
    }
    if (!(recovery >= 0) || !(recovery < 1)) {
        throw std::invalid_argument("recovery must be in [0, 1)");
    }
    return {tradeDate, stepIn, cashSettle, accrualStart, maturity, coupon, recovery};
}

LocalDate standardMaturity(const LocalDate& tradeDate, int tenorYears) {
    if (tenorYears < 0) {
        throw std::invalid_argument("tenorYears must be >= 0");
    }
    int y = tradeDate.year();
    if (tradeDate.isBefore(LocalDate::of(y, 3, 20))) {
        return LocalDate::of(y - 1 + tenorYears, 12, 20);
    }
    if (tradeDate.isBefore(LocalDate::of(y, 9, 20))) {
        return LocalDate::of(y + tenorYears, 6, 20);
    }
    return LocalDate::of(y + tenorYears, 12, 20);
}

LocalDate nextImmDate(const LocalDate& date) {
    int y = date.year();
    int immMonth = ((date.month() - 1) / 3 + 1) * 3;
    LocalDate candidate = LocalDate::of(y, immMonth, 20);
    if (candidate.isAfter(date)) {
        return candidate;
    }
    return immMonth == 12 ? LocalDate::of(y + 1, 3, 20) : LocalDate::of(y, immMonth + 3, 20);
}

LocalDate previousImmDate(const LocalDate& date) {
    LocalDate next = nextImmDate(date);
    return next.minusMonths(3).isAfter(date) ? next.minusMonths(6) : next.minusMonths(3);
}

LocalDate accrualStartDate(const LocalDate& stepInDate, const BusinessCalendar& calendar) {
    LocalDate imm = previousImmDate(stepInDate);
    LocalDate adj = calendar.roll(imm, BusinessCalendar::Roll::FOLLOWING);
    if (adj.isAfter(stepInDate)) {
        adj = calendar.roll(previousImmDate(imm.minusDays(1)), BusinessCalendar::Roll::FOLLOWING);
    }
    return adj;
}

Contract standardContract(const LocalDate& tradeDate, int tenorYears, double coupon,
                          double recovery, const BusinessCalendar& calendar) {
    if (tenorYears < 1 || tenorYears > 30) {
        throw std::invalid_argument("tenorYears must be in [1, 30]");
    }
    LocalDate stepIn = tradeDate.plusDays(1);
    return makeContract(tradeDate, stepIn, calendar.addBusinessDays(tradeDate, 3),
                        accrualStartDate(stepIn, calendar),
                        standardMaturity(tradeDate, tenorYears), coupon, recovery);
}

std::vector<Period> schedule(const Contract& c, const BusinessCalendar& calendar) {
    std::vector<LocalDate> raw;
    LocalDate d = c.maturityDate;
    while (d.isAfter(c.accrualStartDate)) {
        raw.push_back(d);
        d = d.minusMonths(3);
    }
    std::reverse(raw.begin(), raw.end());
    std::vector<Period> periods;
    LocalDate prev = c.accrualStartDate;
    for (std::size_t i = 0; i < raw.size(); i++) {
        bool last = i + 1 == raw.size();
        LocalDate pay = calendar.roll(raw[i], BusinessCalendar::Roll::FOLLOWING);
        LocalDate end = last ? c.maturityDate.plusDays(1) : pay;
        if (!end.isAfter(prev)) {
            continue;
        }
        periods.push_back({prev, end, pay});
        prev = end;
    }
    return periods;
}

double protectionLeg(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival) {
    LocalDate protStart = std::max(c.stepInDate.minusDays(1), c.tradeDate);
    Vec grid = mergedGrid(discount, survival, years(c.tradeDate, protStart),
                          years(c.tradeDate, c.maturityDate));
    double pv = 0;
    for (std::size_t i = 1; i < grid.size(); i++) {
        Seg s = segment(discount, survival, grid[i - 1], grid[i]);
        pv += s.lambda * s.q0p0 * e1(s.k, s.dt);
    }
    return (1 - c.recovery) * pv / discount.value(years(c.tradeDate, c.cashSettleDate));
}

double riskyAnnuityDirty(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                         const Conventions& conventions) {
    double halfDay = conventions.halfDayAccrual ? 0.5 / 365.0 : 0.0;
    const LocalDate& today = c.tradeDate;
    double sum = 0;
    for (const Period& p : schedule(c, conventions.calendar)) {
        if (!p.accrualEnd.isAfter(c.stepInDate)) {
            continue;
        }
        double yf = static_cast<double>(LocalDate::daysBetween(p.accrualStart, p.accrualEnd)) / 360.0;
        double tS = years(today, p.accrualStart.minusDays(1));
        double tE = years(today, p.accrualEnd.minusDays(1));
        sum += yf * survival.value(tE) * discount.value(years(today, p.paymentDate));
        double a = yf / (tE - tS);
        double from = std::max(tS, years(today, c.stepInDate.minusDays(1)));
        Vec grid = mergedGrid(discount, survival, from, tE);
        for (std::size_t i = 1; i < grid.size(); i++) {
            Seg s = segment(discount, survival, grid[i - 1], grid[i]);
            sum += a * s.lambda * s.q0p0
                    * ((grid[i - 1] - tS + halfDay) * e1(s.k, s.dt) + e2(s.k, s.dt));
        }
    }
    return sum / discount.value(years(today, c.cashSettleDate));
}

double accruedYearFraction(const Contract& c) {
    return static_cast<double>(LocalDate::daysBetween(c.accrualStartDate, c.stepInDate)) / 360.0;
}

double riskyAnnuityClean(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                         const Conventions& conventions) {
    return riskyAnnuityDirty(c, discount, survival, conventions) - accruedYearFraction(c);
}

double parSpread(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                 const Conventions& conventions) {
    return protectionLeg(c, discount, survival)
            / riskyAnnuityClean(c, discount, survival, conventions);
}

double cleanUpfront(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                    const Conventions& conventions) {
    return protectionLeg(c, discount, survival)
            - c.coupon * riskyAnnuityClean(c, discount, survival, conventions);
}

double dirtyUpfront(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                    const Conventions& conventions) {
    return protectionLeg(c, discount, survival)
            - c.coupon * riskyAnnuityDirty(c, discount, survival, conventions);
}

double flatHazardForParSpread(const Contract& c, double parSpreadValue, const IsdaCurve& discount,
                              const Conventions& conventions) {
    if (!(parSpreadValue > 0) || parSpreadValue == kInf) {
        throw std::invalid_argument("spread must be positive and finite");
    }
    Contract atPar = c;
    atPar.coupon = parSpreadValue;
    return solveFlatHazard([&](double x) {
        return cleanUpfront(atPar, discount, IsdaCurve::flat(x), conventions);
    });
}

double quotedSpreadToUpfront(const Contract& c, double quotedSpread, const IsdaCurve& discount,
                             const Conventions& conventions) {
    double h = flatHazardForParSpread(c, quotedSpread, discount, conventions);
    return cleanUpfront(c, discount, IsdaCurve::flat(h), conventions);
}

double upfrontToQuotedSpread(const Contract& c, double upfront, const IsdaCurve& discount,
                             const Conventions& conventions) {
    if (!std::isfinite(upfront)) {
        throw std::invalid_argument("upfront must be finite");
    }
    double h = solveFlatHazard([&](double x) {
        return cleanUpfront(c, discount, IsdaCurve::flat(x), conventions) - upfront;
    });
    return parSpread(c, discount, IsdaCurve::flat(h), conventions);
}

IsdaCurve bootstrap(const LocalDate& tradeDate, const std::vector<int>& tenorYears,
                    const Vec& parSpreads, double recovery, const IsdaCurve& discount,
                    const Conventions& conventions) {
    std::size_t n = tenorYears.size();
    if (n == 0 || parSpreads.size() != n) {
        throw std::invalid_argument("need aligned, non-empty tenors and spreads");
    }
    Vec times(n), hazards(n, 0.0);
    std::vector<Contract> contracts;
    for (std::size_t i = 0; i < n; i++) {
        if (i > 0 && tenorYears[i] <= tenorYears[i - 1]) {
            throw std::invalid_argument("tenors must be ascending");
        }
        if (!(parSpreads[i] > 0) || parSpreads[i] == kInf) {
            throw std::invalid_argument("spread must be positive");
        }
        contracts.push_back(standardContract(tradeDate, tenorYears[i], parSpreads[i], recovery,
                                             conventions.calendar));
        times[i] = years(tradeDate, contracts[i].maturityDate);
    }
    for (std::size_t k = 0; k < n; k++) {
        Vec t(times.begin(), times.begin() + k + 1);
        hazards[k] = solveFlatHazard([&](double x) {
            Vec h(hazards.begin(), hazards.begin() + k + 1);
            h[k] = x;
            return cleanUpfront(contracts[k], discount, IsdaCurve::ofHazardRates(t, h),
                                conventions);
        });
    }
    return IsdaCurve::ofHazardRates(times, hazards);
}

IsdaCurve bootstrapSwaps(const LocalDate& tradeDate, const std::vector<Swap>& swaps,
                         const BusinessCalendar& calendar) {
    if (swaps.empty()) {
        throw std::invalid_argument("at least one swap required");
    }
    const auto mf = BusinessCalendar::Roll::MODIFIED_FOLLOWING;
    LocalDate spot = calendar.addBusinessDays(tradeDate, 2);
    std::size_t n = swaps.size();
    Vec t(n), r(n, 0.0);
    for (std::size_t i = 0; i < n; i++) {
        if (swaps[i].months <= 0 || !std::isfinite(swaps[i].rate)) {
            throw std::invalid_argument("swap needs a positive tenor and finite rate");
        }
        LocalDate mat = calendar.roll(spot.plusMonths(swaps[i].months), mf);
        int periods = std::max(1, swaps[i].months / 12);
        std::vector<LocalDate> pays;
        Vec yfs;
        LocalDate prev = spot;
        for (int k = 1; k <= periods; k++) {
            LocalDate d = k == periods ? mat : calendar.roll(spot.plusMonths(12LL * k), mf);
            pays.push_back(d);
            yfs.push_back(static_cast<double>(LocalDate::daysBetween(prev, d)) / 360.0);
            prev = d;
        }
        t[i] = years(tradeDate, mat);
        if (i > 0 && !(t[i] > t[i - 1])) {
            throw std::invalid_argument("swap maturities must be strictly increasing");
        }
        double lo = -0.5, hi = 1.0;
        for (int it = 0; it < 200; it++) {
            double mid = 0.5 * (lo + hi);
            r[i] = mid;
            IsdaCurve cv = IsdaCurve::ofZeroRates(Vec(t.begin(), t.begin() + i + 1),
                                                  Vec(r.begin(), r.begin() + i + 1));
            double pv = cv.value(years(tradeDate, mat)) - cv.value(years(tradeDate, spot));
            for (std::size_t k = 0; k < pays.size(); k++) {
                pv += swaps[i].rate * yfs[k] * cv.value(years(tradeDate, pays[k]));
            }
            if (pv > 0) {  // value decreases in r
                lo = mid;
            } else {
                hi = mid;
            }
        }
        r[i] = 0.5 * (lo + hi);
    }
    return IsdaCurve::ofZeroRates(t, r);
}

}  // namespace isda_cds
}  // namespace quantfinlib
