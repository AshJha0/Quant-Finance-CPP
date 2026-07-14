// Port of fx.CurrencyPairTest and fx.SwapPointsCurveTest (Java): pair
// conventions (pip/precision tables, T+1/T+2 spot lags across dual holiday
// calendars, forward tenor rolls), and the swap-points curve (pillar
// exactness, linear-in-days broken dates, slope extrapolation, covered
// interest parity carry, negative-points curves). Pins and tolerances
// unchanged from the Java source.
#include <cmath>

#include "qtest.hpp"
#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/fx/swap_points_curve.hpp"
#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"

using quantfinlib::BusinessCalendar;
using quantfinlib::CurrencyPair;
using quantfinlib::LocalDate;
using quantfinlib::SwapPointsCurve;

// ------------------------------------------------------------ CurrencyPair

TEST(conventionTableCoversMajorsAndJpyQuotes) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    EXPECT_NEAR(0.0001, eurusd.pipSize(), 0.0);
    EXPECT_TRUE(eurusd.pricePrecision() == 5);
    EXPECT_TRUE(eurusd.spotLagDays() == 2);

    CurrencyPair usdjpy = CurrencyPair::of("USDJPY");
    EXPECT_NEAR(0.01, usdjpy.pipSize(), 0.0);
    EXPECT_TRUE(usdjpy.pricePrecision() == 3);

    EXPECT_TRUE(CurrencyPair::of("USDCAD").spotLagDays() == 1);
    EXPECT_TRUE(CurrencyPair::of("USDTRY").spotLagDays() == 1);
}

TEST(pipConversionsRoundTrip) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    EXPECT_NEAR(1.3, eurusd.pips(0.00013), 1e-9);
    EXPECT_NEAR(0.00013, eurusd.priceFromPips(1.3), 1e-12);
    EXPECT_NEAR(1.08535, eurusd.round(1.0853499999), 1e-9);

    CurrencyPair usdjpy = CurrencyPair::of("USDJPY");
    EXPECT_NEAR(2.5, usdjpy.pips(0.025), 1e-9);
}

TEST(spotDateSkipsWeekends) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    // Friday 2026-01-02 + 2 business days = Tuesday 2026-01-06.
    EXPECT_TRUE(eurusd.spotDate(LocalDate::of(2026, 1, 2)) == LocalDate::of(2026, 1, 6));
    // T+1 pair: Friday -> Monday.
    EXPECT_TRUE(CurrencyPair::of("USDCAD").spotDate(LocalDate::of(2026, 1, 2))
               == LocalDate::of(2026, 1, 5));
}

TEST(spotDateHonoursBothCurrenciesHolidays) {
    // Monday 2026-01-05 is a base-side holiday: Fri trade must skip it.
    BusinessCalendar eurHols = BusinessCalendar::withHolidays({LocalDate::of(2026, 1, 5)});
    CurrencyPair pair = CurrencyPair::of("EURUSD")
                       .withCalendars(eurHols, BusinessCalendar::weekendsOnly());
    // Fri 01-02 + 2 joint business days: Tue 01-06, Wed 01-07.
    EXPECT_TRUE(pair.spotDate(LocalDate::of(2026, 1, 2)) == LocalDate::of(2026, 1, 7));

    // The same holiday on the quote side must block equally.
    CurrencyPair mirrored = CurrencyPair::of("EURUSD")
                           .withCalendars(BusinessCalendar::weekendsOnly(), eurHols);
    EXPECT_TRUE(mirrored.spotDate(LocalDate::of(2026, 1, 2)) == LocalDate::of(2026, 1, 7));
}

TEST(weekTenorsRollForward) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    // Trade Wed 2026-01-07 -> spot Fri 01-09 -> 1W = Fri 01-16.
    EXPECT_TRUE(eurusd.tenorDate(LocalDate::of(2026, 1, 7), "1W") == LocalDate::of(2026, 1, 16));
    // Spot Mon 2026-01-12 (trade Thu 01-08): +2D lands Wed.
    EXPECT_TRUE(eurusd.tenorDate(LocalDate::of(2026, 1, 8), "2D") == LocalDate::of(2026, 1, 14));
}

TEST(monthTenorUsesModifiedFollowing) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    EXPECT_TRUE(eurusd.tenorDate(LocalDate::of(2026, 4, 27), "1M") == LocalDate::of(2026, 5, 29));
    // Trade Wed 2026-01-28 -> spot Fri 01-30 -> 1M unadjusted Sat 02-28 ->
    // following would cross into March, so modified-following rolls BACK.
    EXPECT_TRUE(eurusd.tenorDate(LocalDate::of(2026, 1, 28), "1M") == LocalDate::of(2026, 2, 27));
}

TEST(endEndRulePinsMonthEnds) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    LocalDate trade = LocalDate::of(2026, 2, 25);
    EXPECT_TRUE(eurusd.spotDate(trade) == LocalDate::of(2026, 2, 27));
    EXPECT_TRUE(eurusd.tenorDate(trade, "1M") == LocalDate::of(2026, 3, 31));
}

TEST(preSpotTenorsAndValidation) {
    CurrencyPair eurusd = CurrencyPair::of("EURUSD");
    LocalDate trade = LocalDate::of(2026, 1, 7);  // Wednesday
    EXPECT_TRUE(eurusd.tenorDate(trade, "ON") == LocalDate::of(2026, 1, 8));
    EXPECT_TRUE(eurusd.tenorDate(trade, "TN") == LocalDate::of(2026, 1, 9));
    EXPECT_TRUE(eurusd.spotDate(trade) == eurusd.tenorDate(trade, "TN"));
    EXPECT_TRUE(eurusd.tenorDate(trade, "SN") == LocalDate::of(2026, 1, 12));

    EXPECT_THROWS(CurrencyPair::of("EUR"));
    EXPECT_THROWS(eurusd.tenorDate(trade, "1Q"));
    EXPECT_TRUE(eurusd.toString() == "EURUSD");
}

// --------------------------------------------------------- SwapPointsCurve

namespace {

CurrencyPair eurusdPair() { return CurrencyPair::of("EURUSD"); }
LocalDate tradeDate() { return LocalDate::of(2026, 1, 7); }  // Wed, spot Fri 01-09

SwapPointsCurve curve() {
    return SwapPointsCurve::builder(eurusdPair(), tradeDate(), 1.0850)
        .add("1M", 12.6)   // quoted in pips, out of order on purpose
        .add("1W", 3.1)
        .add("3M", 38.4)
        .build();
}

}  // namespace

TEST(pillarsReproduceExactly) {
    SwapPointsCurve c = curve();
    CurrencyPair eurusd = eurusdPair();
    LocalDate oneMonth = eurusd.tenorDate(tradeDate(), "1M");
    EXPECT_NEAR(1.0850 + 12.6 * 0.0001, c.outright(oneMonth), 1e-12);
    EXPECT_NEAR(1.0850 + 12.6 * 0.0001, c.outright("1M"), 1e-12);
    // Builder sorted the out-of-order quotes by date.
    std::vector<std::string> tenors = c.pillarTenors();
    EXPECT_TRUE(tenors.size() == 3);
    EXPECT_TRUE(tenors[0] == "1W" && tenors[1] == "1M" && tenors[2] == "3M");
    EXPECT_TRUE(c.spotDate() == LocalDate::of(2026, 1, 9));
    EXPECT_NEAR(1.0850, c.spotRate(), 0.0);
}

TEST(brokenDatesInterpolateLinearlyInDays) {
    SwapPointsCurve c = curve();
    CurrencyPair eurusd = eurusdPair();
    LocalDate d1 = eurusd.tenorDate(tradeDate(), "1M");
    LocalDate d3 = eurusd.tenorDate(tradeDate(), "3M");
    LocalDate mid = d1.plusDays(LocalDate::daysBetween(d1, d3) / 2);
    double p1 = c.forwardPoints(d1);
    double p3 = c.forwardPoints(d3);
    double expected = p1 + (p3 - p1)
                     * (static_cast<double>(LocalDate::daysBetween(d1, mid))
                       / static_cast<double>(LocalDate::daysBetween(d1, d3)));
    EXPECT_NEAR(expected, c.forwardPoints(mid), 1e-12);
    // Before the first pillar: anchored at zero points on spot.
    EXPECT_NEAR(0, c.forwardPoints(c.spotDate()), 1e-12);
    LocalDate early = c.spotDate().plusDays(3);
    EXPECT_TRUE(c.forwardPoints(early) > 0 && c.forwardPoints(early) < p1);
}

TEST(beyondLastPillarExtendsTheFinalSlope) {
    SwapPointsCurve c = curve();
    CurrencyPair eurusd = eurusdPair();
    LocalDate d1 = eurusd.tenorDate(tradeDate(), "1M");
    LocalDate d3 = eurusd.tenorDate(tradeDate(), "3M");
    double slope = (c.forwardPoints(d3) - c.forwardPoints(d1))
                  / static_cast<double>(LocalDate::daysBetween(d1, d3));
    LocalDate beyond = d3.plusDays(30);
    EXPECT_NEAR(c.forwardPoints(d3) + slope * 30, c.forwardPoints(beyond), 1e-12);
}

TEST(impliedCarryMatchesCoveredInterestParity) {
    SwapPointsCurve c = curve();
    CurrencyPair eurusd = eurusdPair();
    LocalDate d3 = eurusd.tenorDate(tradeDate(), "3M");
    double tau = static_cast<double>(LocalDate::daysBetween(c.spotDate(), d3)) / 365.0;
    double carry = c.impliedCarry(d3);
    EXPECT_NEAR(std::log(c.outright(d3) / 1.0850) / tau, carry, 1e-12);
    EXPECT_TRUE(carry > 0);
}

TEST(negativePointsCurveIsSupported) {
    CurrencyPair usdjpy = CurrencyPair::of("USDJPY");
    SwapPointsCurve c = SwapPointsCurve::builder(usdjpy, tradeDate(), 155.00)
                       .add("1M", -22.0)
                       .add("6M", -130.0)
                       .build();
    LocalDate d6 = usdjpy.tenorDate(tradeDate(), "6M");
    EXPECT_NEAR(155.00 - 130.0 * 0.01, c.outright(d6), 1e-9);
    EXPECT_TRUE(c.impliedCarry(d6) < 0);
}

TEST(validationRejectsBadInput) {
    CurrencyPair eurusd = eurusdPair();
    EXPECT_THROWS(SwapPointsCurve::builder(eurusd, tradeDate(), 0).add("1M", 1).build());
    EXPECT_THROWS(SwapPointsCurve::builder(eurusd, tradeDate(), 1.08).build());
    // Pre-spot legs belong to the roll, not the forward curve.
    EXPECT_THROWS(SwapPointsCurve::builder(eurusd, tradeDate(), 1.08).add("ON", 0.1));
    EXPECT_THROWS(curve().forwardPoints(tradeDate()));  // before spot
    EXPECT_THROWS(SwapPointsCurve::builder(eurusd, tradeDate(), 1.08)
                     .add("1M", 1).add("1M", 2).build());  // duplicate pillar
}

QTEST_MAIN()
