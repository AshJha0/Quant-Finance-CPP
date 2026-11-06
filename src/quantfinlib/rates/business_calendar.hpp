// Port of com.quantfinlib.rates.BusinessCalendar (Java): weekends plus a
// holiday set, with the standard roll conventions, settlement-lag
// arithmetic, and coupon schedule generation. A joint (dual-center)
// calendar is the union of two holiday sets — the FX settlement rule.
#pragma once

#include <cstdint>
#include <set>
#include <vector>

#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class BusinessCalendar {
public:
    /** Date roll conventions for payment dates landing on non-business days. */
    enum class Roll { NONE, FOLLOWING, MODIFIED_FOLLOWING, PRECEDING };

    /** Where an irregular period goes when the term is not whole periods. */
    enum class StubType { SHORT_FRONT, LONG_FRONT, SHORT_BACK, LONG_BACK };

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

    /** As schedule above with a stub type and the end-of-month rule. */
    std::vector<LocalDate> schedule(const LocalDate& effectiveDate, const LocalDate& maturity,
                                    int paymentsPerYear, Roll convention, StubType stub,
                                    bool endOfMonth) const;

    /** The unadjusted coupon dates strictly after effectiveDate: short front stub, no EOM. */
    static std::vector<LocalDate> unadjustedSchedule(const LocalDate& effectiveDate,
                                                     const LocalDate& maturity,
                                                     int paymentsPerYear);

    /**
     * Unadjusted period end dates strictly after effectiveDate, ascending, the
     * last being maturity. Every date is anchor +- k*months from the fixed
     * anchor (maturity for front stubs, effectiveDate for back stubs), never a
     * step from the previous date: plusMonths clamps the day, and stepping
     * would carry a clamp forward (31 Aug, 28 Feb, then 28 Aug). With
     * endOfMonth and an anchor on its month's last day, every date is the last
     * day of its month.
     */
    static std::vector<LocalDate> unadjustedSchedule(const LocalDate& effectiveDate,
                                                     const LocalDate& maturity,
                                                     int paymentsPerYear, StubType stub,
                                                     bool endOfMonth);

    /** anchor shifted by months; see the end-of-month rule above. */
    static LocalDate shiftMonths(const LocalDate& anchor, std::int64_t months, bool endOfMonth);

private:
    explicit BusinessCalendar(std::set<LocalDate> holidays) : holidays_(std::move(holidays)) {}

    LocalDate nextBusinessDay(const LocalDate& date) const;
    LocalDate previousBusinessDay(const LocalDate& date) const;

    std::set<LocalDate> holidays_;
};

}  // namespace quantfinlib
