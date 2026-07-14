// Minimal port of java.time.LocalDate — exactly the surface the finance
// classes use: civil-date arithmetic (plusDays/plusWeeks/plusMonths with
// Java's day-of-month clamping), ISO day-of-week, and day differences.
// Implemented on Howard Hinnant's civil-days algorithms so every date is
// a thin (year, month, day) value with O(1) serial conversion. Header-only.
#pragma once

#include <compare>
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>

namespace quantfinlib {

class LocalDate {
public:
    /** java.time.LocalDate.of — validates the calendar date. */
    static LocalDate of(int year, int month, int day) {
        if (month < 1 || month > 12 || day < 1 || day > lengthOfMonth(year, month)) {
            throw std::invalid_argument("invalid date: " + std::to_string(year) + "-"
                                        + std::to_string(month) + "-" + std::to_string(day));
        }
        return LocalDate(year, month, day);
    }

    int year() const { return year_; }
    int month() const { return month_; }
    int day() const { return day_; }

    /** Days since 1970-01-01 (negative before). */
    std::int64_t toEpochDay() const { return daysFromCivil(year_, month_, day_); }

    static LocalDate ofEpochDay(std::int64_t serial) {
        LocalDate d(1970, 1, 1);
        civilFromDays(serial, d.year_, d.month_, d.day_);
        return d;
    }

    LocalDate plusDays(std::int64_t n) const { return ofEpochDay(toEpochDay() + n); }
    LocalDate minusDays(std::int64_t n) const { return plusDays(-n); }
    LocalDate plusWeeks(std::int64_t n) const { return plusDays(7 * n); }

    /** Java semantics: day-of-month clamps to the target month's length. */
    LocalDate plusMonths(std::int64_t n) const {
        std::int64_t total = static_cast<std::int64_t>(year_) * 12 + (month_ - 1) + n;
        int y = static_cast<int>(total >= 0 ? total / 12 : (total - 11) / 12);
        int m = static_cast<int>(total - static_cast<std::int64_t>(y) * 12) + 1;
        int d = day_;
        int len = lengthOfMonth(y, m);
        if (d > len) {
            d = len;
        }
        return LocalDate(y, m, d);
    }

    LocalDate minusMonths(std::int64_t n) const { return plusMonths(-n); }

    /** ISO day of week: 1 = Monday ... 7 = Sunday (java.time.DayOfWeek values). */
    int dayOfWeek() const {
        std::int64_t s = toEpochDay() + 3;  // 1970-01-01 is a Thursday (ISO 4)
        std::int64_t m = s % 7;
        if (m < 0) {
            m += 7;
        }
        return static_cast<int>(m) + 1;
    }

    /** Last calendar day of this date's month. */
    LocalDate atEndOfMonth() const {
        return LocalDate(year_, month_, lengthOfMonth(year_, month_));
    }

    static int lengthOfMonth(int year, int month) {
        static constexpr int kLen[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
        if (month == 2 && isLeapYear(year)) {
            return 29;
        }
        return kLen[month - 1];
    }

    static bool isLeapYear(int year) {
        return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    }

    /** ChronoUnit.DAYS.between(a, b): signed, b minus a. */
    static std::int64_t daysBetween(const LocalDate& a, const LocalDate& b) {
        return b.toEpochDay() - a.toEpochDay();
    }

    bool isBefore(const LocalDate& other) const { return *this < other; }
    bool isAfter(const LocalDate& other) const { return *this > other; }

    auto operator<=>(const LocalDate&) const = default;

    /** ISO "2026-01-09" form for diagnostics. */
    std::string toString() const {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", year_, month_, day_);
        return std::string(buf);
    }

private:
    LocalDate(int y, int m, int d) : year_(y), month_(m), day_(d) {}

    // Howard Hinnant's civil-days algorithms (public domain).
    static std::int64_t daysFromCivil(int y, int m, int d) {
        y -= m <= 2;
        const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153U * static_cast<unsigned>(m + (m > 2 ? -3 : 9)) + 2U) / 5U
                             + static_cast<unsigned>(d) - 1U;
        const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
        return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
    }

    static void civilFromDays(std::int64_t z, int& y, int& m, int& d) {
        z += 719468;
        const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const unsigned doe = static_cast<unsigned>(z - era * 146097);
        const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const std::int64_t yy = static_cast<std::int64_t>(yoe) + era * 400;
        const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const unsigned mp = (5 * doy + 2) / 153;
        d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
        m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
        y = static_cast<int>(yy + (m <= 2 ? 1 : 0));
    }

    int year_;
    int month_;
    int day_;
};

}  // namespace quantfinlib
