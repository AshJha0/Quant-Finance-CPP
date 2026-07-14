// Port of fx.FxSwapNdfTest (Java): FX swap and NDF bookings — at-market
// swaps value to zero on their own curve, MTM tracks points moves with the
// right sign, NDF fixing dates walk back by the restricted currency's lag,
// and the USD settlement formula divides by the fixing. Pins unchanged.
#include <cmath>
#include <string>

#include "qtest.hpp"
#include "quantfinlib/fx/currency_pair.hpp"
#include "quantfinlib/fx/fx_swap.hpp"
#include "quantfinlib/fx/ndf.hpp"
#include "quantfinlib/fx/swap_points_curve.hpp"
#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/rates/yield_curve.hpp"

using quantfinlib::BusinessCalendar;
using quantfinlib::CurrencyPair;
using quantfinlib::FxSwap;
using quantfinlib::LocalDate;
using quantfinlib::Ndf;
using quantfinlib::SwapPointsCurve;
using quantfinlib::YieldCurve;

namespace {

CurrencyPair eurusdPair() { return CurrencyPair::of("EURUSD"); }
LocalDate tradeDate() { return LocalDate::of(2026, 1, 7); }  // Wed, spot Fri 01-09

SwapPointsCurve curve(double spot, double oneMonthPips, double threeMonthPips) {
    return SwapPointsCurve::builder(eurusdPair(), tradeDate(), spot)
        .add("1M", oneMonthPips)
        .add("3M", threeMonthPips)
        .build();
}

}  // namespace

TEST(atMarketSwapValuesToZeroOnItsOwnCurve) {
    SwapPointsCurve c = curve(1.0850, 12.6, 38.4);
    FxSwap swap = FxSwap::atMarket(c, "SPOT", "3M", 10'000'000);
    EXPECT_NEAR(0, swap.markToMarket(c), 1e-9);
    EXPECT_NEAR(38.4, swap.swapPointsPips(), 1e-9);
    EXPECT_TRUE(swap.nearDate() == c.spotDate());
    EXPECT_TRUE(swap.toString().find("EURUSD") != std::string::npos);
}

TEST(swapMtmTracksPointsMovesWithTheRightSign) {
    SwapPointsCurve struck = curve(1.0850, 12.6, 38.4);
    FxSwap swap = FxSwap::atMarket(struck, "SPOT", "3M", 10'000'000);
    SwapPointsCurve wider = curve(1.0850, 12.6, 45.0);
    double mtm = swap.markToMarket(wider);
    // Far forward rose by 6.6 pips: short 10m base forward loses 10m x 0.00066.
    EXPECT_NEAR(-10'000'000 * 6.6 * 0.0001, mtm, 1.0);
    YieldCurve usd = YieldCurve::ofZeroRates({0.25, 1}, {0.05, 0.05});
    double discounted = swap.markToMarket(wider, usd);
    EXPECT_TRUE(discounted < 0 && std::fabs(discounted) < std::fabs(mtm));
}

TEST(mismatchedSwapCarriesForwardExposure) {
    SwapPointsCurve struck = curve(1.0850, 12.6, 38.4);
    FxSwap ff = FxSwap::atMarket(struck, "1M", "3M", 5'000'000);
    SwapPointsCurve spotUp = curve(1.0950, 12.6, 38.4);
    EXPECT_NEAR(0, ff.markToMarket(spotUp), 1e-9);
}

TEST(ndfBookingWalksFixingBackByTheCurrencyLag) {
    CurrencyPair usdinr = CurrencyPair::of("USDINR");
    Ndf ndf = Ndf::of(usdinr, tradeDate(), "1M", 84.50, 1'000'000);
    EXPECT_TRUE(Ndf::fixingLagDays("INR") == 2);
    EXPECT_TRUE(Ndf::fixingLagDays("BRL") == 1);
    EXPECT_TRUE(Ndf::fixingLagDays("XXX") == 2);  // unlisted default
    EXPECT_TRUE(ndf.settlementDate() == LocalDate::of(2026, 2, 9));
    EXPECT_TRUE(ndf.fixingDate() == LocalDate::of(2026, 2, 5));
    EXPECT_TRUE(ndf.fixingDate().isBefore(ndf.settlementDate()));
}

TEST(agedSwapMarksAgainstALaterCurveWithoutThrowing) {
    SwapPointsCurve struck = curve(1.0850, 12.6, 38.4);
    FxSwap swap = FxSwap::atMarket(struck, "SPOT", "3M", 10'000'000);
    LocalDate laterTrade = tradeDate().plusWeeks(2);
    SwapPointsCurve later = SwapPointsCurve::builder(eurusdPair(), laterTrade, 1.0900)
                           .add("1M", 11.0)
                           .add("3M", 33.0)
                           .build();
    EXPECT_TRUE(swap.nearDate().isBefore(later.spotDate()));
    double mtm = swap.markToMarket(later);
    double farOnly = -10'000'000 * (later.outright(swap.farDate()) - swap.farRate());
    EXPECT_NEAR(farOnly, mtm, 1e-9);
    YieldCurve usd = YieldCurve::ofZeroRates({1}, {0.05});
    double discountedSign = swap.markToMarket(later, usd);
    EXPECT_TRUE((farOnly > 0) == (discountedSign > 0));
}

TEST(ndfFixingCountsLocalNotJointBusinessDays) {
    BusinessCalendar usHoliday = BusinessCalendar::withHolidays({LocalDate::of(2026, 2, 9)});
    CurrencyPair usdinr = CurrencyPair::of("USDINR")
                         .withCalendars(usHoliday, BusinessCalendar::weekendsOnly());
    Ndf ndf = Ndf::of(usdinr, 1'000'000, 84.50,
                      LocalDate::of(2026, 2, 9), LocalDate::of(2026, 2, 11));
    EXPECT_TRUE(ndf.fixingDate() == LocalDate::of(2026, 2, 9));

    Ndf booked = Ndf::of(usdinr, LocalDate::of(2026, 1, 7), "1M", 84.50, 1'000'000);
    LocalDate expected = booked.settlementDate();
    for (int i = 0; i < 2; i++) {
        do {
            expected = expected.minusDays(1);
        } while (!usdinr.quoteCalendar().isBusinessDay(expected));
    }
    EXPECT_TRUE(booked.fixingDate() == expected);
}

TEST(ndfSettlementDividesByTheFixing) {
    CurrencyPair usdinr = CurrencyPair::of("USDINR");
    Ndf ndf = Ndf::of(usdinr, tradeDate(), "1M", 84.50, 1'000'000);
    EXPECT_NEAR(1'000'000 * (85.00 - 84.50) / 85.00, ndf.settlementAmount(85.00), 1e-9);
    EXPECT_TRUE(ndf.settlementAmount(84.00) < 0);
    EXPECT_THROWS(ndf.settlementAmount(0));
}

TEST(ndfMarksAgainstTheForwardToTheFixingDate) {
    CurrencyPair usdinr = CurrencyPair::of("USDINR");
    SwapPointsCurve c = SwapPointsCurve::builder(usdinr, tradeDate(), 84.20)
                       .add("1M", 3000)
                       .add("3M", 9000)
                       .build();
    Ndf ndf = Ndf::of(usdinr, tradeDate(), "1M", 84.20, 1'000'000);
    double fwdAtFixing = c.outright(ndf.fixingDate());
    EXPECT_NEAR(1'000'000 * (fwdAtFixing - 84.20) / fwdAtFixing, ndf.markToMarket(c), 1e-9);
    YieldCurve usd = YieldCurve::ofZeroRates({1}, {0.05});
    EXPECT_TRUE(ndf.markToMarket(c, usd) > 0 && ndf.markToMarket(c, usd) < ndf.markToMarket(c));
}

TEST(rollCostAndValidation) {
    CurrencyPair eurusd = eurusdPair();
    EXPECT_NEAR(10'000'000 * 0.15 * 0.0001, FxSwap::rollCost(eurusd, 10'000'000, 0.15), 1e-9);
    EXPECT_THROWS(FxSwap::of(eurusd, 1'000'000, tradeDate().plusDays(30), 1.09,
                             tradeDate().plusDays(10), 1.10));
    EXPECT_THROWS(Ndf::of(CurrencyPair::of("USDINR"), 1'000'000, 0,
                          tradeDate().plusDays(28), tradeDate().plusDays(30)));
}

QTEST_MAIN()
