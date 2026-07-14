// Port of the Fama-MacBeth and CalendarAnomalies halves of
// AlphaResearchRoundTest.java: a planted factor is priced and a dud is
// not, singular cross-sections are skipped (not fatal), infinite returns
// fail fast, and calendar profiles recover a planted day-of-week /
// turn-of-month seasonality exactly (with the correct significance).
#include "quantfinlib/alpha/calendar_anomalies.hpp"
#include "quantfinlib/alpha/fama_macbeth.hpp"
#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/risk/java_random.hpp"

#include <cmath>
#include <limits>
#include <vector>

#include "qtest.hpp"

namespace fm = quantfinlib::fama_macbeth;
namespace ca = quantfinlib::calendar_anomalies;
using quantfinlib::JavaRandom;
using quantfinlib::LocalDate;

TEST(famaMacBethPricesThePlantedFactorAndNotTheDud) {
    JavaRandom rnd(17);
    int periods = 60;
    int assets = 50;
    std::vector<std::vector<std::vector<double>>> x(
            static_cast<std::size_t>(periods),
            std::vector<std::vector<double>>(static_cast<std::size_t>(assets), std::vector<double>(2)));
    std::vector<std::vector<double>> r(static_cast<std::size_t>(periods),
                                       std::vector<double>(static_cast<std::size_t>(assets)));
    for (int t = 0; t < periods; t++) {
        for (int a = 0; a < assets; a++) {
            x[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][0] = rnd.nextGaussian();
            x[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][1] = rnd.nextGaussian();
            r[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)] =
                    0.01 * x[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][0]
                    + 0.005 * rnd.nextGaussian();
        }
    }
    r[7][3] = std::numeric_limits<double>::quiet_NaN();

    fm::Result result = fm::fit(x, r);
    EXPECT_TRUE(result.periodsUsed == 60);
    EXPECT_NEAR(result.premia[0], 0.01, 0.001);
    EXPECT_TRUE(result.tStats[0] > 5);
    EXPECT_NEAR(result.premia[1], 0.0, 0.001);
    EXPECT_TRUE(std::abs(result.tStats[1]) < 2.5);
    EXPECT_TRUE(std::abs(result.interceptTStat) < 2.5);

    std::vector<std::vector<std::vector<double>>> tinyX(
            5, std::vector<std::vector<double>>(10, std::vector<double>(2)));
    std::vector<std::vector<double>> tinyR(5, std::vector<double>(10));
    EXPECT_THROWS(fm::fit(tinyX, tinyR));

    for (int a = 0; a < assets; a++) {
        x[30][static_cast<std::size_t>(a)][1] = 1.0;
    }
    fm::Result skipped = fm::fit(x, r);
    EXPECT_TRUE(skipped.periodsUsed == 59);
    EXPECT_NEAR(skipped.premia[0], 0.01, 0.001);

    std::vector<std::vector<double>> rBad = r;
    rBad[5][5] = std::numeric_limits<double>::infinity();
    EXPECT_THROWS(fm::fit(x, rBad));

    std::vector<std::vector<std::vector<double>>> thinX(
            20, std::vector<std::vector<double>>(static_cast<std::size_t>(assets), std::vector<double>(2)));
    std::vector<std::vector<double>> thinR(20, std::vector<double>(static_cast<std::size_t>(assets)));
    for (int t = 0; t < 20; t++) {
        for (int a = 0; a < assets; a++) {
            thinX[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][0] = rnd.nextGaussian();
            thinX[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][1] = rnd.nextGaussian();
            thinR[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)] =
                    t < 10 ? std::numeric_limits<double>::quiet_NaN()
                           : 0.01 * thinX[static_cast<std::size_t>(t)][static_cast<std::size_t>(a)][0];
        }
    }
    EXPECT_THROWS(fm::fit(thinX, thinR));
}

TEST(calendarProfilesRecoverPlantedSeasonalityExactly) {
    int n = 400;
    std::vector<double> returns(static_cast<std::size_t>(n));
    std::vector<std::int64_t> stamps(static_cast<std::size_t>(n));
    LocalDate d = LocalDate::of(2024, 1, 1);
    int perDay[7] = {0, 0, 0, 0, 0, 0, 0};
    for (int i = 0; i < n;) {
        int dow0 = d.dayOfWeek() - 1;  // 0=Monday..6=Sunday
        if (dow0 != 5 && dow0 != 6) {  // skip Saturday/Sunday
            double base = dow0 == 0 ? -0.002 : 0.0005;
            returns[static_cast<std::size_t>(i)] =
                    base + ((perDay[dow0]++ % 2 == 0) ? 1e-4 : -1e-4);
            stamps[static_cast<std::size_t>(i)] = d.toEpochDay() * 86'400'000LL;
            i++;
        }
        d = d.plusDays(1);
    }
    ca::DayOfWeekProfile profile = ca::dayOfWeek(returns, stamps);
    EXPECT_TRUE(profile.count[0] == 80);
    EXPECT_NEAR(profile.meanReturn[0], -0.002, 1e-12);
    EXPECT_TRUE(profile.tStat[0] < -5);
    EXPECT_NEAR(profile.meanReturn[2], 0.0005, 1e-12);
    EXPECT_TRUE(profile.count[5] == 0);
    EXPECT_TRUE(std::isnan(profile.meanReturn[6]));

    std::vector<double> tom(static_cast<std::size_t>(n));
    int parity[2] = {0, 0};
    for (int i = 0; i < n; i++) {
        LocalDate date = LocalDate::ofEpochDay(stamps[static_cast<std::size_t>(i)] / 86'400'000LL);
        bool inside = date.day() <= 3 || date.day() > LocalDate::lengthOfMonth(date.year(), date.month()) - 1;
        double base = inside ? 0.003 : 0.0001;
        tom[static_cast<std::size_t>(i)] = base + ((parity[inside ? 0 : 1]++ % 2 == 0) ? 1e-4 : -1e-4);
    }
    ca::TurnOfMonth split = ca::turnOfMonth(tom, stamps, 1, 3);
    EXPECT_NEAR(split.insideMean, 0.003, 1.1e-4);
    EXPECT_NEAR(split.outsideMean, 0.0001, 1.1e-4);
    EXPECT_TRUE(split.tStat > 10);
    EXPECT_TRUE(split.insideCount + split.outsideCount == n);

    EXPECT_THROWS(ca::turnOfMonth(tom, stamps, 0, 0));
}

QTEST_MAIN()
