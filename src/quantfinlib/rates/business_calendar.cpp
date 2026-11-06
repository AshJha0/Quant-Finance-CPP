#include "quantfinlib/rates/business_calendar.hpp"

#include <algorithm>
#include <stdexcept>

namespace quantfinlib {

BusinessCalendar BusinessCalendar::unionWith(const BusinessCalendar& other) const {
    std::set<LocalDate> combined = holidays_;
    combined.insert(other.holidays_.begin(), other.holidays_.end());
    return BusinessCalendar(std::move(combined));
}

bool BusinessCalendar::isBusinessDay(const LocalDate& date) const {
    int dow = date.dayOfWeek();
    return dow != 6 && dow != 7 && holidays_.find(date) == holidays_.end();
}

LocalDate BusinessCalendar::roll(const LocalDate& date, Roll convention) const {
    if (convention == Roll::NONE || isBusinessDay(date)) {
        return date;
    }
    switch (convention) {
        case Roll::FOLLOWING:
            return nextBusinessDay(date);
        case Roll::PRECEDING:
            return previousBusinessDay(date);
        case Roll::MODIFIED_FOLLOWING: {
            LocalDate following = nextBusinessDay(date);
            return following.month() == date.month() ? following : previousBusinessDay(date);
        }
        case Roll::NONE:
        default:
            return date;
    }
}

LocalDate BusinessCalendar::addBusinessDays(const LocalDate& date, int n) const {
    if (n < 0) {
        throw std::invalid_argument("n must be >= 0");
    }
    LocalDate d = date;
    for (int i = 0; i < n; i++) {
        d = nextBusinessDay(d);
    }
    return d;
}

LocalDate BusinessCalendar::subtractBusinessDays(const LocalDate& date, int n) const {
    if (n < 0) {
        throw std::invalid_argument("n must be >= 0");
    }
    LocalDate d = date;
    for (int i = 0; i < n; i++) {
        d = previousBusinessDay(d);
    }
    return d;
}

std::vector<LocalDate> BusinessCalendar::schedule(const LocalDate& effectiveDate,
                                                  const LocalDate& maturity,
                                                  int paymentsPerYear, Roll convention) const {
    return schedule(effectiveDate, maturity, paymentsPerYear, convention, StubType::SHORT_FRONT,
                    false);
}

std::vector<LocalDate> BusinessCalendar::schedule(const LocalDate& effectiveDate,
                                                  const LocalDate& maturity,
                                                  int paymentsPerYear, Roll convention,
                                                  StubType stub, bool endOfMonth) const {
    std::vector<LocalDate> unadjusted =
            unadjustedSchedule(effectiveDate, maturity, paymentsPerYear, stub, endOfMonth);
    std::vector<LocalDate> out;
    out.reserve(unadjusted.size());
    for (const LocalDate& d : unadjusted) {
        out.push_back(roll(d, convention));
    }
    return out;
}

std::vector<LocalDate> BusinessCalendar::unadjustedSchedule(const LocalDate& effectiveDate,
                                                            const LocalDate& maturity,
                                                            int paymentsPerYear) {
    return unadjustedSchedule(effectiveDate, maturity, paymentsPerYear, StubType::SHORT_FRONT,
                              false);
}

std::vector<LocalDate> BusinessCalendar::unadjustedSchedule(const LocalDate& effectiveDate,
                                                            const LocalDate& maturity,
                                                            int paymentsPerYear, StubType stub,
                                                            bool endOfMonth) {
    if (paymentsPerYear < 1 || 12 % paymentsPerYear != 0) {
        throw std::invalid_argument("paymentsPerYear must divide 12");
    }
    if (!maturity.isAfter(effectiveDate)) {
        throw std::invalid_argument("maturity must be after effective date");
    }
    int months = 12 / paymentsPerYear;
    std::vector<LocalDate> dates;
    if (stub == StubType::SHORT_FRONT || stub == StubType::LONG_FRONT) {
        std::int64_t k = 0;
        LocalDate d = maturity;
        while (d.isAfter(effectiveDate)) {
            dates.push_back(d);
            k++;
            d = shiftMonths(maturity, -months * k, endOfMonth);
        }
        std::reverse(dates.begin(), dates.end());
        bool hasStub = d != effectiveDate;
        if (stub == StubType::LONG_FRONT && hasStub && dates.size() >= 2) {
            dates.erase(dates.begin());
        }
    } else {
        std::int64_t k = 1;
        LocalDate d = shiftMonths(effectiveDate, months, endOfMonth);
        while (d.isBefore(maturity)) {
            dates.push_back(d);
            k++;
            d = shiftMonths(effectiveDate, months * k, endOfMonth);
        }
        bool hasStub = d != maturity;
        if (stub == StubType::LONG_BACK && hasStub && !dates.empty()) {
            dates.pop_back();
        }
        dates.push_back(maturity);
    }
    return dates;
}

LocalDate BusinessCalendar::shiftMonths(const LocalDate& anchor, std::int64_t months,
                                        bool endOfMonth) {
    LocalDate d = anchor.plusMonths(months);
    if (endOfMonth && anchor.day() == LocalDate::lengthOfMonth(anchor.year(), anchor.month())) {
        return d.atEndOfMonth();
    }
    return d;
}

LocalDate BusinessCalendar::nextBusinessDay(const LocalDate& date) const {
    LocalDate d = date.plusDays(1);
    while (!isBusinessDay(d)) {
        d = d.plusDays(1);
    }
    return d;
}

LocalDate BusinessCalendar::previousBusinessDay(const LocalDate& date) const {
    LocalDate d = date.minusDays(1);
    while (!isBusinessDay(d)) {
        d = d.minusDays(1);
    }
    return d;
}

}  // namespace quantfinlib
