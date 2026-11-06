// Port of com.quantfinlib.credit.IsdaCdsModel, IsdaCurve and IsdaRateCurve
// (Java): the ISDA Standard CDS Model. Curves interpolate r*t linearly, so
// forwards and hazards are piecewise flat, and both legs integrate in closed
// form between merged curve nodes, with the Taylor fix near k*dt = 0. Times
// are ACT/365 from trade date, premium accruals ACT/360 with the last period
// one day longer. Values are per unit notional, as of cash settle. Standard
// contracts: step-in T+1, cash settle T+3 business days, 20 Jun/Dec maturities
// rolled on the post-2015 semi-annual rule.
// Written from the published model description and the Java port; this is not
// ISDA's code.
#pragma once

#include <string>
#include <vector>

#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"

namespace quantfinlib {

class IsdaCurve {
public:
    static IsdaCurve ofZeroRates(const std::vector<double>& times,
                                 const std::vector<double>& zeroRates);
    /** Piecewise-constant hazards; the last stays in force beyond the last node. */
    static IsdaCurve ofHazardRates(const std::vector<double>& times,
                                   const std::vector<double>& hazards);
    static IsdaCurve flat(double rate);

    double rateTimesTime(double t) const;
    double zeroRate(double t) const;
    double value(double t) const;
    const std::vector<double>& times() const { return times_; }

private:
    IsdaCurve(std::vector<double> times, std::vector<double> rt)
        : times_(std::move(times)), rt_(std::move(rt)) {}

    std::vector<double> times_;  // ascending, > 0
    std::vector<double> rt_;     // r_i * t_i
};

namespace isda_cds {

inline constexpr double STANDARD_RECOVERY = 0.40;
inline constexpr double COUPON_100BP = 0.01;
inline constexpr double COUPON_500BP = 0.05;

struct Conventions {
    BusinessCalendar calendar = BusinessCalendar::weekendsOnly();
    bool halfDayAccrual = false;
};

struct Contract {
    LocalDate tradeDate, stepInDate, cashSettleDate, accrualStartDate, maturityDate;
    double coupon;
    double recovery;
};

struct Period {
    LocalDate accrualStart, accrualEnd, paymentDate;
};

/** Checks dates, coupon and recovery; throws std::invalid_argument. */
Contract makeContract(const LocalDate& tradeDate, const LocalDate& stepIn,
                      const LocalDate& cashSettle, const LocalDate& accrualStart,
                      const LocalDate& maturity, double coupon, double recovery);

Contract standardContract(const LocalDate& tradeDate, int tenorYears, double coupon,
                          double recovery,
                          const BusinessCalendar& calendar = BusinessCalendar::weekendsOnly());
LocalDate standardMaturity(const LocalDate& tradeDate, int tenorYears);
LocalDate nextImmDate(const LocalDate& date);
LocalDate previousImmDate(const LocalDate& date);
LocalDate accrualStartDate(const LocalDate& stepInDate, const BusinessCalendar& calendar);
std::vector<Period> schedule(const Contract& c, const BusinessCalendar& calendar);

double protectionLeg(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival);
double riskyAnnuityDirty(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                         const Conventions& conventions = {});
double accruedYearFraction(const Contract& c);
double riskyAnnuityClean(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                         const Conventions& conventions = {});
double parSpread(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                 const Conventions& conventions = {});
double cleanUpfront(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                    const Conventions& conventions = {});
double dirtyUpfront(const Contract& c, const IsdaCurve& discount, const IsdaCurve& survival,
                    const Conventions& conventions = {});
double flatHazardForParSpread(const Contract& c, double parSpread, const IsdaCurve& discount,
                              const Conventions& conventions = {});
/** The market's quoted-spread convention: flat hazard at the quote, then upfront. */
double quotedSpreadToUpfront(const Contract& c, double quotedSpread, const IsdaCurve& discount,
                             const Conventions& conventions = {});
double upfrontToQuotedSpread(const Contract& c, double cleanUpfront, const IsdaCurve& discount,
                             const Conventions& conventions = {});
/** Hazards repricing each standard tenor's par spread to zero clean upfront. */
IsdaCurve bootstrap(const LocalDate& tradeDate, const std::vector<int>& tenorYears,
                    const std::vector<double>& parSpreads, double recovery,
                    const IsdaCurve& discount, const Conventions& conventions = {});

/** A swap quote for the ISDA rate curve: tenor in months, annual ACT/360 fixed. */
struct Swap {
    int months;
    double rate;
};

/** Zero curve repricing each swap to par from spot T+2, one bisection per node. */
IsdaCurve bootstrapSwaps(const LocalDate& tradeDate, const std::vector<Swap>& swaps,
                         const BusinessCalendar& calendar);

}  // namespace isda_cds
}  // namespace quantfinlib
