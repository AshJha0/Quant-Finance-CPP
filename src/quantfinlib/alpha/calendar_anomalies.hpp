// Port of com.quantfinlib.alpha.CalendarAnomalies: day-of-week and
// turn-of-month seasonality profiles with t-statistics. Turn-of-month
// windows use CALENDAR days of month, not trading days. Timestamps are
// interpreted in UTC (epoch milliseconds, floor-divided into UTC days via
// LocalDate).
#pragma once

#include <cstdint>
#include <vector>

namespace quantfinlib {
namespace calendar_anomalies {

/** Per-day-of-week profile, indexed Monday = 0 ... Sunday = 6. */
struct DayOfWeekProfile {
    std::vector<double> meanReturn;
    std::vector<double> tStat;
    std::vector<int> count;
};

/** The turn-of-month split, with a Welch t-stat on the difference. */
struct TurnOfMonth {
    double insideMean = 0;
    double outsideMean = 0;
    double tStat = 0;
    int insideCount = 0;
    int outsideCount = 0;
};

/** @param returns per-period returns aligned with epochMillis. */
DayOfWeekProfile dayOfWeek(const std::vector<double>& returns, const std::vector<std::int64_t>& epochMillis);

/**
 * @param daysBeforeMonthEnd  calendar days at month end in the window, >= 0
 * @param daysAfterMonthStart calendar days at month start in the window, >= 0
 */
TurnOfMonth turnOfMonth(const std::vector<double>& returns, const std::vector<std::int64_t>& epochMillis,
                        int daysBeforeMonthEnd, int daysAfterMonthStart);

}  // namespace calendar_anomalies
}  // namespace quantfinlib
