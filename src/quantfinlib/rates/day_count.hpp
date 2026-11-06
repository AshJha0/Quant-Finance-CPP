// Port of com.quantfinlib.rates.DayCount (Java): the year fraction between two
// dates as term sheets define it. Definitions follow the 2006 ISDA Definitions
// section 4.16: THIRTY_360 is 30/360 Bond Basis (4.16(f)), THIRTY_E_360 is
// 30E/360 Eurobond Basis (4.16(g)), THIRTY_E_360_ISDA is 4.16(h), ACT_ACT_ISDA
// is 4.16(b). ACT_ACT_ICMA (ICMA Rule 251, 4.16(c)) needs the coupon period, so
// the two-date form throws; use actActIcma, or pass it to bond_pricer. BUS/252
// needs a calendar and is bus252.
#pragma once

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

enum class DayCount {
    ACT_360,
    ACT_365,
    THIRTY_360,
    THIRTY_E_360,
    THIRTY_E_360_ISDA,
    ACT_ACT_ICMA,
    ACT_ACT_ISDA
};

namespace day_count {

/** Year fraction from start (inclusive) to end (exclusive). */
double yearFraction(DayCount convention, const LocalDate& start, const LocalDate& end);

/**
 * Year fraction when the rule depends on whether end is the termination date.
 * Only THIRTY_E_360_ISDA uses it: a February termination date is left alone.
 */
double yearFraction(DayCount convention, const LocalDate& start, const LocalDate& end,
                    const LocalDate& terminationDate);

/**
 * ACT/ACT ICMA: days(start, end) / (frequency * days(periodStart, periodEnd)),
 * where [periodStart, periodEnd) is the regular coupon period containing the
 * interval. A full regular period is exactly 1/frequency.
 */
double actActIcma(const LocalDate& start, const LocalDate& end, const LocalDate& periodStart,
                  const LocalDate& periodEnd, int frequency);

/** Business days d with start <= d < end on calendar, over 252 (Brazil, ANBIMA). */
double bus252(const BusinessCalendar& calendar, const LocalDate& start, const LocalDate& end);

}  // namespace day_count
}  // namespace quantfinlib
