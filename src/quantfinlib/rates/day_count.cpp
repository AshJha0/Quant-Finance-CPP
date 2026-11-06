#include "quantfinlib/rates/day_count.hpp"

#include <algorithm>
#include <cstdint>
#include <stdexcept>

namespace quantfinlib {
namespace day_count {

namespace {

bool isLastDayOfMonth(const LocalDate& d) {
    return d.day() == LocalDate::lengthOfMonth(d.year(), d.month());
}

double thirty360(const LocalDate& start, const LocalDate& end, int d1, int d2) {
    int days = 360 * (end.year() - start.year()) + 30 * (end.month() - start.month()) + (d2 - d1);
    return days / 360.0;
}

double daysInYear(int year) {
    return LocalDate::isLeapYear(year) ? 366.0 : 365.0;
}

double thirtyEIsda(const LocalDate& start, const LocalDate& end, bool endIsTermination) {
    int d1 = isLastDayOfMonth(start) ? 30 : start.day();
    int d2 = end.day();
    bool febTermination = endIsTermination && end.month() == 2;
    if (isLastDayOfMonth(end) && !febTermination) {
        d2 = 30;
    }
    return thirty360(start, end, d1, d2);
}

}  // namespace

double yearFraction(DayCount convention, const LocalDate& start, const LocalDate& end) {
    switch (convention) {
        case DayCount::ACT_360:
            return static_cast<double>(LocalDate::daysBetween(start, end)) / 360.0;
        case DayCount::ACT_365:
            return static_cast<double>(LocalDate::daysBetween(start, end)) / 365.0;
        case DayCount::THIRTY_360: {
            int d1 = std::min(start.day(), 30);
            int d2 = end.day();
            if (d2 == 31 && d1 == 30) {
                d2 = 30;
            }
            return thirty360(start, end, d1, d2);
        }
        case DayCount::THIRTY_E_360:
            return thirty360(start, end, std::min(start.day(), 30), std::min(end.day(), 30));
        case DayCount::THIRTY_E_360_ISDA:
            return thirtyEIsda(start, end, false);
        case DayCount::ACT_ACT_ICMA:
            throw std::invalid_argument("ACT/ACT ICMA needs the coupon period: use actActIcma");
        case DayCount::ACT_ACT_ISDA: {
            if (start.year() == end.year()) {
                return static_cast<double>(LocalDate::daysBetween(start, end))
                        / daysInYear(start.year());
            }
            double first = static_cast<double>(LocalDate::daysBetween(
                                   start, LocalDate::of(start.year() + 1, 1, 1)))
                    / daysInYear(start.year());
            double last = static_cast<double>(LocalDate::daysBetween(
                                  LocalDate::of(end.year(), 1, 1), end))
                    / daysInYear(end.year());
            return first + (end.year() - start.year() - 1) + last;
        }
    }
    throw std::invalid_argument("unknown day count");
}

double yearFraction(DayCount convention, const LocalDate& start, const LocalDate& end,
                    const LocalDate& terminationDate) {
    if (convention == DayCount::THIRTY_E_360_ISDA) {
        return thirtyEIsda(start, end, end == terminationDate);
    }
    return yearFraction(convention, start, end);
}

double actActIcma(const LocalDate& start, const LocalDate& end, const LocalDate& periodStart,
                  const LocalDate& periodEnd, int frequency) {
    std::int64_t periodDays = LocalDate::daysBetween(periodStart, periodEnd);
    if (periodDays <= 0 || frequency < 1) {
        throw std::invalid_argument("need a positive reference period and frequency");
    }
    return static_cast<double>(LocalDate::daysBetween(start, end))
            / static_cast<double>(frequency * periodDays);
}

double bus252(const BusinessCalendar& calendar, const LocalDate& start, const LocalDate& end) {
    if (end.isBefore(start)) {
        return -bus252(calendar, end, start);
    }
    int n = 0;
    for (LocalDate d = start; d.isBefore(end); d = d.plusDays(1)) {
        if (calendar.isBusinessDay(d)) {
            n++;
        }
    }
    return n / 252.0;
}

}  // namespace day_count
}  // namespace quantfinlib
