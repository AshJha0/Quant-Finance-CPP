// Golden tests for the model layer. Sources, none of them this library:
//   holidays: QuantLib 1.44 Calendar.holidayList (UnitedStates FederalReserve,
//     UnitedKingdom Settlement, TARGET, Japan), 2021-2027
//   GARCH: scipy Nelder-Mead then Powell on an independent Python likelihood
//   curves: QuantLib PiecewiseLogLinearDiscount with OIS, deposit and swap
//     helpers on a null calendar, Actual/365 Fixed curve time
//   SVI: hand arithmetic and a planted smile
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <set>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/pricing/svi_slice.hpp"
#include "quantfinlib/pricing/vol_surface.hpp"
#include "quantfinlib/rates/curve_instruments.hpp"
#include "quantfinlib/rates/discount_curve.hpp"
#include "quantfinlib/rates/holiday_calendars.hpp"
#include "quantfinlib/rates/multi_curve_bootstrap.hpp"
#include "quantfinlib/volatility/garch11.hpp"

using namespace quantfinlib;
using holiday_calendars::Market;

namespace {

// 64-bit LCG plus Box-Muller, the same recipe as the Python generator.
std::vector<double> garchSeries() {
    std::uint64_t s = 12345;
    auto u = [&s]() {
        s = s * 6364136223846793005ULL + 1442695040888963407ULL;
        return (static_cast<double>(s >> 11) + 0.5) / 9007199254740992.0;
    };
    double w = 2e-6, a = 0.08, b = 0.9;
    double h = w / (1 - a - b);
    std::vector<double> out;
    for (int i = 0; i < 1500; i++) {
        double r = std::sqrt(-2 * std::log(u()));
        double z = r * std::cos(2 * 3.141592653589793 * u());
        double x = std::sqrt(h) * z;
        out.push_back(x);
        h = w + a * x * x + b * h;
    }
    return out;
}

}  // namespace

TEST(golden_holidaysAgainstQuantLib) {
    struct Row { Market m; int year; std::vector<int> days; };
    std::vector<Row> rows = {
    {Market::NEW_YORK_FED, 2021, {20210101, 20210118, 20210215, 20210531, 20210705, 20210906, 20211011, 20211111, 20211125}},
    {Market::NEW_YORK_FED, 2022, {20220117, 20220221, 20220530, 20220620, 20220704, 20220905, 20221010, 20221111, 20221124, 20221226}},
    {Market::NEW_YORK_FED, 2023, {20230102, 20230116, 20230220, 20230529, 20230619, 20230704, 20230904, 20231009, 20231123, 20231225}},
    {Market::NEW_YORK_FED, 2024, {20240101, 20240115, 20240219, 20240527, 20240619, 20240704, 20240902, 20241014, 20241111, 20241128, 20241225}},
    {Market::NEW_YORK_FED, 2025, {20250101, 20250120, 20250217, 20250526, 20250619, 20250704, 20250901, 20251013, 20251111, 20251127, 20251225}},
    {Market::NEW_YORK_FED, 2026, {20260101, 20260119, 20260216, 20260525, 20260619, 20260907, 20261012, 20261111, 20261126, 20261225}},
    {Market::NEW_YORK_FED, 2027, {20270101, 20270118, 20270215, 20270531, 20270705, 20270906, 20271011, 20271111, 20271125}},
    {Market::LONDON, 2021, {20210101, 20210402, 20210405, 20210503, 20210531, 20210830, 20211227, 20211228}},
    {Market::LONDON, 2022, {20220103, 20220415, 20220418, 20220502, 20220602, 20220603, 20220829, 20220919, 20221226, 20221227}},
    {Market::LONDON, 2023, {20230102, 20230407, 20230410, 20230501, 20230508, 20230529, 20230828, 20231225, 20231226}},
    {Market::LONDON, 2024, {20240101, 20240329, 20240401, 20240506, 20240527, 20240826, 20241225, 20241226}},
    {Market::LONDON, 2025, {20250101, 20250418, 20250421, 20250505, 20250526, 20250825, 20251225, 20251226}},
    {Market::LONDON, 2026, {20260101, 20260403, 20260406, 20260504, 20260525, 20260831, 20261225, 20261228}},
    {Market::LONDON, 2027, {20270101, 20270326, 20270329, 20270503, 20270531, 20270830, 20271227, 20271228}},
    {Market::TARGET, 2021, {20210101, 20210402, 20210405}},
    {Market::TARGET, 2022, {20220415, 20220418, 20221226}},
    {Market::TARGET, 2023, {20230407, 20230410, 20230501, 20231225, 20231226}},
    {Market::TARGET, 2024, {20240101, 20240329, 20240401, 20240501, 20241225, 20241226}},
    {Market::TARGET, 2025, {20250101, 20250418, 20250421, 20250501, 20251225, 20251226}},
    {Market::TARGET, 2026, {20260101, 20260403, 20260406, 20260501, 20261225}},
    {Market::TARGET, 2027, {20270101, 20270326, 20270329}},
    {Market::TOKYO, 2021, {20210101, 20210111, 20210211, 20210223, 20210429, 20210503, 20210504, 20210505, 20210722, 20210723, 20210809, 20210920, 20210923, 20211103, 20211123, 20211231}},
    {Market::TOKYO, 2022, {20220103, 20220110, 20220211, 20220223, 20220321, 20220429, 20220503, 20220504, 20220505, 20220718, 20220811, 20220919, 20220923, 20221010, 20221103, 20221123}},
    {Market::TOKYO, 2023, {20230102, 20230103, 20230109, 20230223, 20230321, 20230503, 20230504, 20230505, 20230717, 20230811, 20230918, 20231009, 20231103, 20231123}},
    {Market::TOKYO, 2024, {20240101, 20240102, 20240103, 20240108, 20240212, 20240223, 20240320, 20240429, 20240503, 20240506, 20240715, 20240812, 20240916, 20240923, 20241014, 20241104, 20241231}},
    {Market::TOKYO, 2025, {20250101, 20250102, 20250103, 20250113, 20250211, 20250224, 20250320, 20250429, 20250505, 20250506, 20250721, 20250811, 20250915, 20250923, 20251013, 20251103, 20251124, 20251231}},
    {Market::TOKYO, 2026, {20260101, 20260102, 20260112, 20260211, 20260223, 20260320, 20260429, 20260504, 20260505, 20260506, 20260720, 20260811, 20260921, 20260922, 20260923, 20261012, 20261103, 20261123, 20261231}},
    {Market::TOKYO, 2027, {20270101, 20270111, 20270211, 20270223, 20270322, 20270429, 20270503, 20270504, 20270505, 20270719, 20270811, 20270920, 20270923, 20271011, 20271103, 20271123, 20271231}},
    };
    for (const Row& r : rows) {
        std::set<int> got;
        for (const LocalDate& d : holiday_calendars::holidays(r.m, r.year)) {
            got.insert(d.year() * 10000 + d.month() * 100 + d.day());
        }
        std::set<int> want(r.days.begin(), r.days.end());
        for (int d : want) {
            if (!got.count(d)) {
                std::printf("missing %d\n", d);
            }
        }
        for (int d : got) {
            if (!want.count(d)) {
                std::printf("extra %d\n", d);
            }
        }
        EXPECT_TRUE(got == want);
    }
    EXPECT_TRUE(holiday_calendars::easterSunday(2027) == LocalDate::of(2027, 3, 28));
    BusinessCalendar c = holiday_calendars::calendar(Market::LONDON, 2026, 2026);
    EXPECT_TRUE(!c.isBusinessDay(LocalDate::of(2026, 12, 28)));  // Boxing Day substitute
    EXPECT_THROWS(holiday_calendars::calendar(Market::LONDON, 2027, 2026));
    EXPECT_THROWS(holiday_calendars::holidays(Market::TOKYO, 1999));
}

TEST(golden_garchMleAgainstScipy) {
    std::vector<double> r = garchSeries();
    Garch11::Fit f = Garch11::estimate(r, Garch11::Innovation::GAUSSIAN);
    EXPECT_NEAR(5034.234740417482, f.params.logLikelihood, 1e-5);
    EXPECT_NEAR(1.5108930155217335e-06, f.params.omega, 2e-9);
    EXPECT_NEAR(0.06950229291272064, f.params.alpha, 2e-4);
    EXPECT_NEAR(0.9119786212028551, f.params.beta, 2e-4);
    // omega is free: not the variance-targeted value.
    double mean = 0, var = 0;
    for (double x : r) { mean += x / r.size(); }
    for (double x : r) { var += (x - mean) * (x - mean) / (r.size() - 1); }
    EXPECT_TRUE(std::fabs(f.params.omega - var * (1 - f.params.alpha - f.params.beta)) > 1e-8);
    // Planted alpha = 0.08 and beta = 0.9 sit within a few standard errors.
    EXPECT_TRUE(f.standardErrors.size() == 3 && f.standardErrors[1] > 0 && f.standardErrors[1] < 0.05);
    EXPECT_TRUE(std::fabs(f.params.alpha - 0.08) < 3 * f.standardErrors[1]);
    EXPECT_TRUE(f.robustStandardErrors[2] > 0);
    // Student-t nests the normal: a Gaussian series gives a large nu and a likelihood at least as good.
    Garch11::Fit t = Garch11::estimate(r, Garch11::Innovation::STUDENT_T);
    EXPECT_TRUE(t.nu > 10);
    EXPECT_TRUE(t.params.logLikelihood > f.params.logLikelihood - 1e-3);
    std::vector<double> bad = r;
    bad[7] = std::nan("");
    EXPECT_THROWS(Garch11::fit(bad));
    EXPECT_THROWS(Garch11::fit(std::vector<double>(200, 0.01)));
}

TEST(sviFormulasAndCalibration) {
    SviSlice s(0.02, 0.1, -0.4, 0.05, 0.2);
    // k = 0.05: a + b*sigma.
    EXPECT_NEAR(0.04, s.totalVariance(0.05), 1e-15);
    double h = 1e-5;
    EXPECT_NEAR((s.totalVariance(0.3 + h) - s.totalVariance(0.3 - h)) / (2 * h), s.slope(0.3), 1e-9);
    EXPECT_NEAR((s.slope(0.3 + h) - s.slope(0.3 - h)) / (2 * h), s.curvature(0.3), 1e-9);
    // g at k = 0 by hand from w, w', w''.
    double w = s.totalVariance(0), w1 = s.slope(0), w2 = s.curvature(0);
    EXPECT_NEAR(1 - 0.25 * w1 * w1 * (1 / w + 0.25) + 0.5 * w2, s.densityFactor(0), 1e-14);
    EXPECT_NEAR(std::sqrt(0.04 / 0.5), s.impliedVol(0.05, 0.5), 1e-15);
    EXPECT_THROWS(SviSlice(-1, 0.1, 0, 0, 0.1));
    EXPECT_THROWS(SviSlice(0.01, 0.1, 1, 0, 0.1));
    // Axel Vogt's slice (Gatheral-Jacquier 2014, section 5.1) has butterfly arbitrage.
    SviSlice vogt(-0.0410, 0.1331, 0.3060, 0.3586, 0.4153);
    EXPECT_TRUE(vogt.minDensityFactor(-1.5, 1.5, 301) < 0);
    // A planted arbitrage-free slice is recovered.
    std::vector<double> k, q;
    for (int i = -6; i <= 6; i++) {
        k.push_back(i * 0.1);
        q.push_back(s.totalVariance(i * 0.1));
    }
    SviSlice fit = SviSlice::calibrate(k, q);
    EXPECT_TRUE(fit.rmse(k, q) < 1e-8);
    EXPECT_NEAR(s.rho(), fit.rho(), 1e-4);
    // Fitted to Vogt's quotes with the penalty, the result is close to free of arbitrage.
    std::vector<double> vq;
    for (double x : k) {
        vq.push_back(vogt.totalVariance(x) );
    }
    SviSlice clean = SviSlice::calibrate(k, vq);
    SviSlice raw = SviSlice::calibrate(k, vq, {}, false);
    EXPECT_TRUE(clean.minDensityFactor(-1.0, 1.0, 201) > raw.minDensityFactor(-1.0, 1.0, 201));
    EXPECT_THROWS(SviSlice::calibrate({0, 1}, {0.1, 0.2}));
}

TEST(surfaceBuildRejectsArbitrageOnlyWhenAsked) {
    auto calendar = [] { return VolSurface::builder().add(0.5, 100, 0.30).add(1.0, 100, 0.20); };
    EXPECT_TRUE(calendar().build().expiries().size() == 2);  // default stays permissive
    EXPECT_THROWS(calendar().rejectArbitrage().build());
    auto fly = [] {
        return VolSurface::builder().add(1, 95, 0.20).add(1, 100, 0.60).add(1, 105, 0.20)
                .forward(1, 100);
    };
    EXPECT_TRUE(fly().build().expiries().size() == 1);
    EXPECT_THROWS(fly().rejectArbitrage().build());
    VolSurface ok = VolSurface::builder().add(0.5, 90, 0.22).add(0.5, 100, 0.20).add(0.5, 110, 0.19)
            .add(1, 90, 0.22).add(1, 100, 0.20).add(1, 110, 0.19)
            .forward(0.5, 100).forward(1, 100).rejectArbitrage().build();
    EXPECT_TRUE(ok.expiries().size() == 2);
    EXPECT_THROWS(VolSurface::builder().rejectArbitrage(1));
}

TEST(golden_multiCurveAgainstQuantLib) {
    LocalDate val = LocalDate::of(2026, 11, 4);
    BusinessCalendar cal = BusinessCalendar::weekendsOnly();
    auto none = BusinessCalendar::Roll::NONE;
    auto interp = CurveInterpolation::LOG_LINEAR_DISCOUNT;
    std::vector<CurveInstrumentPtr> ois;
    const double q[][2] = {{1, .031}, {2, .0295}, {3, .029}, {5, .0295}, {7, .031}, {10, .0325}};
    for (const auto& x : q) {
        ois.push_back(std::make_shared<OisSwap>(val, val.plusMonths(12 * static_cast<int>(x[0])), x[1],
                                                1, DayCount::ACT_360, cal, none));
    }
    DiscountCurve d = multi_curve_bootstrap::bootstrap(val, ois, interp);
    const double wantDf[] = {0.9695272208232911, 0.942727734177421, 0.9167436409919633, 0.8628970800236394, 0.8043872298234688, 0.7209060951981743};
    for (int i = 0; i < 6; i++) {
        EXPECT_NEAR(wantDf[i], d.discountFactor(val.plusMonths(12 * static_cast<int>(q[i][0]))), 1e-11);
    }
    EXPECT_NEAR(0.9297516948832611, d.discountFactor(val.plusMonths(30)), 1e-11);
    std::vector<CurveInstrumentPtr> proj;
    proj.push_back(std::make_shared<Deposit>(val, val.plusMonths(6), 0.0345, DayCount::ACT_360));
    const double s[][2] = {{2, .034}, {3, .0335}, {5, .034}, {10, .036}};
    for (const auto& x : s) {
        proj.push_back(std::make_shared<InterestRateSwap>(
                val, val.plusMonths(12 * static_cast<int>(x[0])), x[1], 1, DayCount::THIRTY_360, 2,
                DayCount::ACT_360, cal, none));
    }
    DiscountCurve p = multi_curve_bootstrap::bootstrapProjection(val, proj, interp, d);
    const double wantP[] = {0.9352721947190791, 0.9058414518199607, 0.8458706413429272, 0.700693752326107};
    for (int i = 0; i < 4; i++) {
        EXPECT_NEAR(wantP[i], p.discountFactor(val.plusMonths(12 * static_cast<int>(s[i][0]))), 1e-10);
    }
    EXPECT_NEAR(0.9829499146062699, p.discountFactor(val.plusMonths(6)), 1e-11);
    EXPECT_NEAR(0.8753426128128108, p.discountFactor(val.plusMonths(48)), 1e-10);
    // Every quote reprices, on both interpolations; negative quotes solve too.
    for (const auto& c : proj) {
        EXPECT_NEAR(c->quote(), c->impliedQuote(d, p), 1e-12);
    }
    DiscountCurve mc = multi_curve_bootstrap::bootstrap(val, ois, CurveInterpolation::MONOTONE_CONVEX);
    for (const auto& c : ois) {
        EXPECT_NEAR(c->quote(), c->impliedQuote(mc, mc), 1e-12);
    }
    std::vector<CurveInstrumentPtr> neg = {std::make_shared<OisSwap>(
            val, val.plusMonths(24), -0.004, 1, DayCount::ACT_360, cal, none)};
    EXPECT_TRUE(multi_curve_bootstrap::bootstrap(val, neg, interp).discountFactor(2.0) > 1);
    // Forwards stay continuous at a pillar under monotone convex, and jump under log-linear.
    double t = mc.pillarTime(2);
    EXPECT_NEAR(mc.instantaneousForward(t - 1e-7), mc.instantaneousForward(t + 1e-7), 1e-5);
    EXPECT_TRUE(std::fabs(d.instantaneousForward(t - 1e-7) - d.instantaneousForward(t + 1e-7)) > 1e-4);
    // Fail closed.
    EXPECT_THROWS(multi_curve_bootstrap::bootstrap(val, {}, interp));
    EXPECT_THROWS(multi_curve_bootstrap::bootstrap(val, {ois[0], ois[0]}, interp));
    EXPECT_THROWS(multi_curve_bootstrap::bootstrapProjection(val, ois, interp, d));
    EXPECT_THROWS(multi_curve_bootstrap::bootstrapProjection(val.plusDays(1), proj, interp, d));
    EXPECT_THROWS(Deposit(val, val, 0.01, DayCount::ACT_360));
    // A quote no curve can reach (1 + r*tau < 0) fails rather than returning a curve.
    std::vector<CurveInstrumentPtr> impossible = {
            std::make_shared<Deposit>(val, val.plusMonths(6), -10.0, DayCount::ACT_360)};
    EXPECT_THROWS(multi_curve_bootstrap::bootstrap(val, impossible, interp));
}

TEST(futuresConvexityByHand) {
    // a = 0: sigma^2 t1 t2 / 2.
    EXPECT_NEAR(0.5 * 1e-4 * 1 * 1.25, RateFuture::hullWhiteConvexity(1, 1.25, 0, 0.01), 1e-15);
    // Small a tends to the a = 0 limit.
    EXPECT_NEAR(RateFuture::hullWhiteConvexity(1, 1.25, 0, 0.01),
                RateFuture::hullWhiteConvexity(1, 1.25, 1e-6, 0.01), 1e-9);
    LocalDate v = LocalDate::of(2026, 11, 4);
    DiscountCurve flat = DiscountCurve::of(v, {10.0}, {std::exp(-0.3)}, CurveInterpolation::LOG_LINEAR_DISCOUNT);
    RateFuture f = RateFuture::ofPrice(v.plusMonths(12), v.plusMonths(15), 97, DayCount::ACT_360, 0, 0);
    double t1 = flat.time(v.plusMonths(12)), t2 = flat.time(v.plusMonths(15));
    EXPECT_NEAR(std::expm1(0.03 * (t2 - t1)) / (90 / 360.0 + 0 * t1),
                f.impliedQuote(flat, flat) * (day_count::yearFraction(DayCount::ACT_360, v.plusMonths(12), v.plusMonths(15)) / 0.25), 1e-12);
    EXPECT_NEAR(0.03, f.quote(), 1e-15);
}

QTEST_MAIN()
