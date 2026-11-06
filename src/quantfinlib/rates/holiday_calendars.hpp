// Port of com.quantfinlib.rates.HolidayCalendars (Java): rule-based weekday
// closures for four markets. NEW_YORK_FED is the Federal Reserve schedule
// (a Saturday holiday is not observed; Sunday moves to Monday), LONDON the
// England and Wales bank holidays with the one-off years, TARGET the ECB
// closing days, TOKYO the national holidays plus bank closing days, 2000-2099.
// One-off closures announced after this was written are not known to it.
#pragma once

#include <set>

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {
namespace holiday_calendars {

enum class Market { NEW_YORK_FED, LONDON, TARGET, TOKYO };

/** A calendar with the market's holidays for fromYear..toYear inclusive. */
BusinessCalendar calendar(Market market, int fromYear, int toYear);

/** Weekday closures of one year. */
std::set<LocalDate> holidays(Market market, int year);

/** Gregorian Easter Sunday (Anonymous Gregorian algorithm). */
LocalDate easterSunday(int year);

}  // namespace holiday_calendars
}  // namespace quantfinlib
