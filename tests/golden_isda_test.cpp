// ISDA Standard CDS Model against QuantLib 1.44 IsdaCdsEngine (Taylor fix, no
// accrual bias, piecewise forwards) with the CDS2015 schedule, trade date
// 2026-04-14, flat curves. No ISDA reference data is stored here: the
// constants come from running QuantLib.
#include <cmath>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/credit/isda_cds_model.hpp"

using namespace quantfinlib;
namespace isda = quantfinlib::isda_cds;

TEST(golden_isdaLegsAgainstQuantLib) {
    struct { int tenor; double cpn, r, h, rec, fair, protection, coupon, rebate, npv; } rows[] = {
        {5, 0.01, 0.03, 0.02, 0.4, 0.011881546390672067, 0.05482082778442676, -0.04686151623227395, 0.0007220441619840443, 0.008681355714136857},
        {3, 0.05, 0.01, 0.08, 0.25, 0.059254831339756335, 0.1662089827377762, -0.14386012309311563, 0.0036108143196551174, 0.025959673964315692},
        {10, 0.01, -0.002, 0.005, 0.4, 0.0029581443147520966, 0.030112644390509865, -0.1025179586652377, 0.0007222340944659893, -0.07168308018026184},
    };
    LocalDate td = LocalDate::of(2026, 4, 14);
    for (const auto& x : rows) {
        isda::Contract c = isda::standardContract(td, x.tenor, x.cpn, x.rec);
        IsdaCurve disc = IsdaCurve::flat(x.r), surv = IsdaCurve::flat(x.h);
        // QuantLib reports as of trade date, the ISDA convention as of cash settle.
        double df = disc.value(3.0 / 365.0);
        EXPECT_NEAR(x.protection, isda::protectionLeg(c, disc, surv) * df, 3e-8);
        EXPECT_NEAR(x.npv, isda::cleanUpfront(c, disc, surv) * df, 3e-8);
        EXPECT_NEAR(x.fair, isda::parSpread(c, disc, surv), 1e-8);
        EXPECT_NEAR(-x.coupon, x.cpn * isda::riskyAnnuityDirty(c, disc, surv) * df, 3e-8);
    }
}

TEST(isdaDatesByHand) {
    // Tuesday 2026-04-14: step-in the 15th, cash T+3 business days, accrual
    // from 20 Mar (a Friday), and 5y rolls to 20 Jun 2031 on the 2015 rule.
    isda::Contract c = isda::standardContract(LocalDate::of(2026, 4, 14), 5, 0.01, 0.4);
    EXPECT_TRUE(c.stepInDate == LocalDate::of(2026, 4, 15));
    EXPECT_TRUE(c.cashSettleDate == LocalDate::of(2026, 4, 17));
    EXPECT_TRUE(c.accrualStartDate == LocalDate::of(2026, 3, 20));
    EXPECT_TRUE(c.maturityDate == LocalDate::of(2031, 6, 20));
    EXPECT_NEAR(26 / 360.0, isda::accruedYearFraction(c), 1e-15);
    // March and September roll dates, and the early-year case.
    EXPECT_TRUE(isda::standardMaturity(LocalDate::of(2026, 3, 19), 5) == LocalDate::of(2030, 12, 20));
    EXPECT_TRUE(isda::standardMaturity(LocalDate::of(2026, 3, 20), 5) == LocalDate::of(2031, 6, 20));
    EXPECT_TRUE(isda::standardMaturity(LocalDate::of(2026, 9, 20), 5) == LocalDate::of(2031, 12, 20));
    EXPECT_TRUE(isda::nextImmDate(LocalDate::of(2026, 12, 20)) == LocalDate::of(2027, 3, 20));
    EXPECT_TRUE(isda::previousImmDate(LocalDate::of(2026, 3, 20)) == LocalDate::of(2026, 3, 20));
    // The last period runs one day past maturity; 20 Jun 2026 is a Saturday.
    auto s = isda::schedule(c, BusinessCalendar::weekendsOnly());
    EXPECT_TRUE(s.back().accrualEnd == LocalDate::of(2031, 6, 21));
    EXPECT_TRUE(s.back().paymentDate == LocalDate::of(2031, 6, 20));
    EXPECT_TRUE(s.front().paymentDate == LocalDate::of(2026, 6, 22));
    EXPECT_TRUE(s.size() == 21);
}

TEST(isdaRoundTripsAndBootstrap) {
    LocalDate td = LocalDate::of(2026, 4, 14);
    BusinessCalendar cal = BusinessCalendar::weekendsOnly();
    IsdaCurve disc = isda::bootstrapSwaps(td, {{12, 0.03}, {24, 0.031}, {60, 0.033}, {120, 0.035}}, cal);
    // Curve hits a one-year par swap: 1 + c*yf discounted at maturity equals DF(spot).
    LocalDate spot = LocalDate::of(2026, 4, 16), mat = LocalDate::of(2027, 4, 16);
    double t0 = 2 / 365.0, t1 = LocalDate::daysBetween(td, mat) / 365.0;
    double yf = LocalDate::daysBetween(spot, mat) / 360.0;
    EXPECT_NEAR(disc.value(t0), (1 + 0.03 * yf) * disc.value(t1), 1e-13);
    isda::Contract c = isda::standardContract(td, 5, 0.01, 0.4);
    double up = isda::quotedSpreadToUpfront(c, 0.025, disc);
    EXPECT_NEAR(0.025, isda::upfrontToQuotedSpread(c, up, disc), 1e-11);
    IsdaCurve surv = isda::bootstrap(td, {1, 3, 5, 10}, {0.008, 0.012, 0.016, 0.02}, 0.4, disc);
    int tenors[] = {1, 3, 5, 10};
    double spreads[] = {0.008, 0.012, 0.016, 0.02};
    for (int i = 0; i < 4; i++) {
        isda::Contract k = isda::standardContract(td, tenors[i], spreads[i], 0.4);
        EXPECT_NEAR(spreads[i], isda::parSpread(k, disc, surv), 1e-11);
    }
    // The flat curve beyond the last node keeps the last hazard, not the average.
    IsdaCurve h = IsdaCurve::ofHazardRates({1, 2}, {0.01, 0.05});
    EXPECT_NEAR(0.05, h.rateTimesTime(4) - h.rateTimesTime(3), 1e-12);
    // Half-day accrual adds to the annuity; clean and dirty differ by the accrued.
    isda::Conventions half;
    half.halfDayAccrual = true;
    IsdaCurve flat = IsdaCurve::flat(0.02);
    EXPECT_TRUE(isda::riskyAnnuityDirty(c, disc, flat, half) > isda::riskyAnnuityDirty(c, disc, flat));
    EXPECT_NEAR(c.coupon * isda::accruedYearFraction(c),
                isda::cleanUpfront(c, disc, flat) - isda::dirtyUpfront(c, disc, flat), 1e-14);
    // Fail closed.
    EXPECT_THROWS(isda::standardContract(td, 0, 0.01, 0.4));
    EXPECT_THROWS(isda::standardContract(td, 5, 0.01, 1.0));
    EXPECT_THROWS(isda::flatHazardForParSpread(c, 0.0, disc));
    EXPECT_THROWS(isda::upfrontToQuotedSpread(c, 2.0, disc));
    EXPECT_THROWS(isda::bootstrap(td, {5, 3}, {0.01, 0.01}, 0.4, disc));
    EXPECT_THROWS(IsdaCurve::ofHazardRates({1}, {-0.01}));
    EXPECT_THROWS(isda::bootstrapSwaps(td, {}, cal));
}

QTEST_MAIN()
