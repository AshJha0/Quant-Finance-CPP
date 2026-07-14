// Port of com.quantfinlib.rates.BusinessCalendar (Java): weekends plus a
// holiday set, with the standard roll conventions, settlement-lag
// arithmetic, and coupon schedule generation. A joint (dual-center)
// calendar is the union of two holiday sets — the FX settlement rule.
#pragma once

#include <set>
#include <vector>

#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class BusinessCalendar {
public:
    /** Date roll conventions for payment dates landing on non-business days. */
    enum class Roll { NONE, FOLLOWING, MODIFIED_FOLLOWING, PRECEDING };

    static BusinessCalendar weekendsOnly() { return BusinessCalendar({}); }

    static BusinessCalendar withHolidays(std::set<LocalDate> holidays) {
        return BusinessCalendar(std::move(holidays));
    }

    /**
     * The joint calendar of two trading centers: a day is a business day
     * only when it is one in BOTH (holiday sets union).
     */
    BusinessCalendar unionWith(const BusinessCalendar& other) const;

    bool isBusinessDay(const LocalDate& date) const;

    /** Applies the roll convention to a date. */
    LocalDate roll(const LocalDate& date, Roll convention) const;

    /** Adds n >= 0 business days — e.g. T+2 settlement from a trade date. */
    LocalDate addBusinessDays(const LocalDate& date, int n) const;

    /** Walks back n >= 0 business days — e.g. a fixing lag before settlement. */
    LocalDate subtractBusinessDays(const LocalDate& date, int n) const;

    /**
     * Coupon payment schedule: unadjusted dates generated backward from
     * maturity every 12/paymentsPerYear months, then rolled. Ascending,
     * strictly after effectiveDate.
     */
    std::vector<LocalDate> schedule(const LocalDate& effectiveDate, const LocalDate& maturity,
                                    int paymentsPerYear, Roll convention) const;

    /** The unadjusted (theoretical) coupon dates strictly after effectiveDate. */
    static std::vector<LocalDate> unadjustedSchedule(const LocalDate& effectiveDate,
                                                     const LocalDate& maturity,
                                                     int paymentsPerYear);

private:
    explicit BusinessCalendar(std::set<LocalDate> holidays) : holidays_(std::move(holidays)) {}

    LocalDate nextBusinessDay(const LocalDate& date) const;
    LocalDate previousBusinessDay(const LocalDate& date) const;

    std::set<LocalDate> holidays_;
};

}  // namespace quantfinlib
