#include "quantfinlib/rates/holiday_calendars.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace holiday_calendars {

namespace {

constexpr int kMon = 1, kThu = 4, kSun = 7;
using Dates = std::set<LocalDate>;

bool isWeekend(const LocalDate& d) {
    return d.dayOfWeek() >= 6;
}

LocalDate nth(int y, int month, int n, int dow) {
    LocalDate first = LocalDate::of(y, month, 1);
    return first.plusDays((dow - first.dayOfWeek() + 7) % 7 + 7 * (n - 1));
}

LocalDate last(int y, int month, int dow) {
    LocalDate end = LocalDate::of(y, month, 1).atEndOfMonth();
    return end.minusDays((end.dayOfWeek() - dow + 7) % 7);
}

LocalDate fedObserved(const LocalDate& d) {
    return d.dayOfWeek() == kSun ? d.plusDays(1) : d;
}

Dates newYorkFed(int y) {
    Dates h;
    h.insert(fedObserved(LocalDate::of(y, 1, 1)));
    h.insert(nth(y, 1, 3, kMon));
    h.insert(nth(y, 2, 3, kMon));
    h.insert(last(y, 5, kMon));
    if (y >= 2022) {
        h.insert(fedObserved(LocalDate::of(y, 6, 19)));
    }
    h.insert(fedObserved(LocalDate::of(y, 7, 4)));
    h.insert(nth(y, 9, 1, kMon));
    h.insert(nth(y, 10, 2, kMon));
    h.insert(fedObserved(LocalDate::of(y, 11, 11)));
    h.insert(nth(y, 11, 4, kThu));
    h.insert(fedObserved(LocalDate::of(y, 12, 25)));
    return h;
}

Dates london(int y) {
    Dates h;
    LocalDate ny = LocalDate::of(y, 1, 1);
    while (isWeekend(ny)) {
        ny = ny.plusDays(1);
    }
    h.insert(ny);
    LocalDate easter = easterSunday(y);
    h.insert(easter.minusDays(2));
    h.insert(easter.plusDays(1));
    if (y == 1995 || y == 2020) {
        h.insert(LocalDate::of(y, 5, 8));  // VE Day anniversaries
    } else {
        h.insert(nth(y, 5, 1, kMon));
    }
    if (y == 2002) {
        h.insert(LocalDate::of(2002, 6, 3));
        h.insert(LocalDate::of(2002, 6, 4));
    } else if (y == 2012) {
        h.insert(LocalDate::of(2012, 6, 4));
        h.insert(LocalDate::of(2012, 6, 5));
    } else if (y == 2022) {
        h.insert(LocalDate::of(2022, 6, 2));
        h.insert(LocalDate::of(2022, 6, 3));
    } else {
        h.insert(last(y, 5, kMon));
    }
    h.insert(last(y, 8, kMon));
    // Christmas and Boxing Day: each weekend day moves to the next free weekday.
    LocalDate x = LocalDate::of(y, 12, 25);
    while (isWeekend(x)) {
        x = x.plusDays(1);
    }
    h.insert(x);
    LocalDate boxing = LocalDate::of(y, 12, 26);
    LocalDate b = boxing.isAfter(x) ? boxing : x.plusDays(1);
    while (isWeekend(b) || b == x) {
        b = b.plusDays(1);
    }
    h.insert(b);
    if (y == 1999) {
        h.insert(LocalDate::of(1999, 12, 31));
    }
    if (y == 2011) {
        h.insert(LocalDate::of(2011, 4, 29));
    }
    if (y == 2022) {
        h.insert(LocalDate::of(2022, 9, 19));
    }
    if (y == 2023) {
        h.insert(LocalDate::of(2023, 5, 8));
    }
    return h;
}

Dates target(int y) {
    LocalDate easter = easterSunday(y);
    return {LocalDate::of(y, 1, 1), easter.minusDays(2), easter.plusDays(1),
            LocalDate::of(y, 5, 1), LocalDate::of(y, 12, 25), LocalDate::of(y, 12, 26)};
}

int equinoxDay(int y, double base) {
    return static_cast<int>(std::floor(base + 0.242194 * (y - 1980)
                                       - std::floor((y - 1980) / 4.0)));
}

Dates tokyo(int y) {
    if (y < 2000 || y > 2099) {
        throw std::invalid_argument("Tokyo calendar supports 2000 to 2099, got "
                                    + std::to_string(y));
    }
    Dates nat;
    auto add = [&nat, y](int m, int d) { nat.insert(LocalDate::of(y, m, d)); };
    add(1, 1);
    nat.insert(nth(y, 1, 2, kMon));  // Coming of Age
    add(2, 11);                      // Foundation
    if (y >= 2020) {
        add(2, 23);  // Emperor's birthday
    }
    add(3, equinoxDay(y, 20.8431));
    add(4, 29);  // Showa
    add(5, 3);
    add(5, 4);
    add(5, 5);
    if (y == 2020) {
        add(7, 23);
        add(7, 24);
        add(8, 10);
    } else if (y == 2021) {
        add(7, 22);
        add(7, 23);
        add(8, 8);
    } else {
        nat.insert(y >= 2003 ? nth(y, 7, 3, kMon) : LocalDate::of(y, 7, 20));
        if (y >= 2016) {
            add(8, 11);  // Mountain Day
        }
        nat.insert(nth(y, 10, 2, kMon));  // Sports Day
    }
    nat.insert(y >= 2003 ? nth(y, 9, 3, kMon) : LocalDate::of(y, 9, 15));
    add(9, equinoxDay(y, 23.2488));
    add(11, 3);
    add(11, 23);
    if (y <= 2018) {
        add(12, 23);
    }
    if (y == 2019) {
        add(4, 30);
        add(5, 1);
        add(5, 2);
        add(10, 22);
    }
    // Citizens' holiday: a non-Sunday day between two national holidays.
    Dates sandwiched;
    for (const LocalDate& d : nat) {
        LocalDate mid = d.plusDays(1);
        if (nat.count(d.plusDays(2)) && !nat.count(mid) && mid.dayOfWeek() != kSun) {
            sandwiched.insert(mid);
        }
    }
    nat.insert(sandwiched.begin(), sandwiched.end());
    // Substitute holiday: a holiday on Sunday moves to the next non-holiday day.
    Dates subs;
    for (const LocalDate& d : nat) {
        if (d.dayOfWeek() == kSun) {
            LocalDate s = d.plusDays(1);
            while (nat.count(s) || subs.count(s)) {
                s = s.plusDays(1);
            }
            subs.insert(s);
        }
    }
    nat.insert(subs.begin(), subs.end());
    add(1, 2);  // bank closing days
    add(1, 3);
    add(12, 31);
    return nat;
}

}  // namespace

std::set<LocalDate> holidays(Market market, int year) {
    Dates h;
    switch (market) {
        case Market::NEW_YORK_FED: h = newYorkFed(year); break;
        case Market::LONDON: h = london(year); break;
        case Market::TARGET: h = target(year); break;
        case Market::TOKYO: h = tokyo(year); break;
    }
    for (auto it = h.begin(); it != h.end();) {
        it = isWeekend(*it) ? h.erase(it) : std::next(it);
    }
    return h;
}

BusinessCalendar calendar(Market market, int fromYear, int toYear) {
    if (toYear < fromYear) {
        throw std::invalid_argument("toYear must be >= fromYear");
    }
    Dates all;
    for (int y = fromYear; y <= toYear; y++) {
        Dates h = holidays(market, y);
        all.insert(h.begin(), h.end());
    }
    return BusinessCalendar::withHolidays(std::move(all));
}

LocalDate easterSunday(int year) {
    int a = year % 19, b = year / 100, c = year % 100, d = b / 4, e = b % 4;
    int f = (b + 8) / 25, g = (b - f + 1) / 3;
    int h = (19 * a + b - d - g + 15) % 30;
    int i = c / 4, k = c % 4;
    int l = (32 + 2 * e + 2 * i - h - k) % 7;
    int m = (a + 11 * h + 22 * l) / 451;
    return LocalDate::of(year, (h + l - 7 * m + 114) / 31, (h + l - 7 * m + 114) % 31 + 1);
}

}  // namespace holiday_calendars
}  // namespace quantfinlib
