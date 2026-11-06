// Golden values from outside this library and its Java parent, so a shared
// bug cannot hide. Regenerate with QuantLib 1.44 and mpmath 60-digit arithmetic:
//   normCdf, normInv, Black-Scholes, touch: mpmath closed forms
//   quanto: closed form, cross-checked with ql.QuantoEuropeanEngine to 1e-14
//   barriers: ql.AnalyticBarrierEngine, Actual/365 Fixed, T = 365 days
//   Bachelier, shifted Black: ql.bachelierBlackFormula, ql.blackFormula
//   day counts, schedules, bonds: ql.DayCounter, ql.Schedule, ql.BondFunctions
// Tolerances are tight enough that the old Abramowitz-Stegun normCdf (error
// 7.5e-8) fails every table that goes through it.
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

#include "qtest.hpp"
#include "quantfinlib/credit/cds_pricer.hpp"
#include "quantfinlib/credit/credit_curve.hpp"
#include "quantfinlib/pricing/bachelier.hpp"
#include "quantfinlib/pricing/barrier_option.hpp"
#include "quantfinlib/pricing/black76.hpp"
#include "quantfinlib/pricing/black_scholes.hpp"
#include "quantfinlib/pricing/quanto_option.hpp"
#include "quantfinlib/pricing/sabr_model.hpp"
#include "quantfinlib/pricing/touch_option.hpp"
#include "quantfinlib/pricing/vol_surface.hpp"
#include "quantfinlib/rates/bond_pricer.hpp"
#include "quantfinlib/rates/business_calendar.hpp"
#include "quantfinlib/rates/day_count.hpp"
#include "quantfinlib/rates/rates_options.hpp"
#include "quantfinlib/rates/yield_curve.hpp"
#include "quantfinlib/risk/frtb_es.hpp"
#include "quantfinlib/risk/gaussian_copula.hpp"
#include "quantfinlib/risk/risk_metrics.hpp"
#include "quantfinlib/risk/tail_risk.hpp"
#include "quantfinlib/risk/var_engine.hpp"
#include "quantfinlib/util/math_utils.hpp"
#include "quantfinlib/volatility/ewma_volatility.hpp"

using namespace quantfinlib;
using barrier_option::Direction;
using barrier_option::Knock;

namespace {

constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

void expectRel(double want, double got, double rel) {
    EXPECT_NEAR(want, got, rel * std::fabs(want));
}

struct Ymd {
    int y, m, d;
    LocalDate date() const { return LocalDate::of(y, m, d); }
};

YieldCurve flatCurve(double r) {
    return YieldCurve::ofZeroRates({1, 2, 3, 5, 7, 10, 20}, {r, r, r, r, r, r, r});
}

}  // namespace

// ---------------------------------------------------------------- C15

TEST(golden_normCdfRelativeAccuracyDownTheTail) {
    struct { double x, p; } rows[] = {
    {0.0, 0.5},
    {0.25, 0.5987063256829237},
    {-0.5, 0.3085375387259869},
    {0.674, 0.7498443431596774},
    {1.0, 0.8413447460685429},
    {-1.5, 0.06680720126885807},
    {2.0, 0.9772498680518208},
    {-3.0, 0.0013498980316300946},
    {-5.0, 2.866515718791939e-07},
    {-5.7, 5.990371401063528e-09},
    {-6.0, 9.86587645037698e-10},
    {-8.0, 6.220960574271784e-16},
    {-9.0, 1.1285884059538405e-19},
    {-12.0, 1.776482112077679e-33},
    {-20.0, 2.7536241186062337e-89},
    {-37.0, 5.725571222524577e-300},
    {5.5, 0.9999999810104375},
    {7.0, 0.9999999999987201},
    {8.5, 1.0},
    };
    for (const auto& r : rows) {
        expectRel(r.p, math_utils::normCdf(r.x), 4e-15);
    }
    EXPECT_TRUE(math_utils::normCdf(-38) > 0);  // subnormal, not flushed
    EXPECT_NEAR(0.0, math_utils::normCdf(-41), 0.0);
    EXPECT_NEAR(1.0, math_utils::normCdf(41), 0.0);
    EXPECT_TRUE(std::isnan(math_utils::normCdf(kNaN)));
}

TEST(golden_normInvToDoublePrecision) {
    struct { double p, x; } rows[] = {
    {1e-300, -37.0470962993612},
    {1e-100, -21.273453560965326},
    {1e-20, -9.262340089798407},
    {1e-10, -6.361340902404057},
    {1e-6, -4.753424308822899},
    {0.001, -3.0902323061678136},
    {0.02425, -1.972961051311885},
    {0.025, -1.9599639845400543},
    {0.3, -0.5244005127080408},
    {0.5, 0.0},
    {0.7, 0.5244005127080407},
    {0.975, 1.9599639845400538},
    {0.99, 2.3263478740408408},
    {0.999, 3.090232306167813},
    {0.999999, 4.753424308817087},
    };
    for (const auto& r : rows) {
        EXPECT_NEAR(r.x, math_utils::normInv(r.p), 2e-14 * std::max(1.0, std::fabs(r.x)));
    }
    EXPECT_THROWS(math_utils::normInv(0.0));
    EXPECT_THROWS(math_utils::normInv(1.0));
    EXPECT_THROWS(math_utils::normInv(kNaN));
    // Subnormal p must stay finite (the Halley step overflows there).
    EXPECT_TRUE(std::isfinite(math_utils::normInv(1e-320)));
}

TEST(golden_expTimesNormCdfKeepsTheFiniteProduct) {
    // exp(800) * N(-41) = exp(800 - 840.5)/sqrt(2 pi) * R(41), value from mpmath.
    expectRel(2.5057729707752388e-20, math_utils::expTimesNormCdf(800, -41), 1e-12);
    expectRel(0.8413447460685429, math_utils::expTimesNormCdf(0, 1), 1e-15);
}

TEST(golden_blackScholesTable) {
    struct { double s, k, r, q, v, t, call, put; } rows[] = {
    {100, 105, 0.03, 0.01, 0.25, 0.75, 7.102945482548201, 10.514019905934676},
    {100, 300, 0, 0, 0.2, 0.25, 3.4529165077418788e-28, 200.0},
    {100, 150, 0, 0, 0.1, 0.25, 1.8672551913332183e-16, 50.0},
    {100, 50, 0, 0, 0.2, 0.25, 50.00000000000204, 2.041483315793935e-12},
    {100, 130, 0.02, 0, 0.05, 0.25, 1.0108925861228723e-25, 29.3516222950487},
    };
    for (const auto& x : rows) {
        expectRel(x.call, black_scholes::price(OptionType::CALL, x.s, x.k, x.r, x.q, x.v, x.t), 1e-11);
        expectRel(x.put, black_scholes::price(OptionType::PUT, x.s, x.k, x.r, x.q, x.v, x.t), 1e-11);
    }
}

// ---------------------------------------------------------------- C12

TEST(golden_quantoUsesForeignRateInDriftDomesticInDiscount) {
    struct { double s, k, rd, rf, q, ss, sx, rho, t, fwd, call, put; } rows[] = {
    {100, 100, 0.05, 0.01, 0, 0.2, 0.1, -0.3, 1, 101.61286854060948, 8.429637537894656, 6.895429524215393},
    {100, 110, 0.02, 0.04, 0.01, 0.3, 0.12, 0.5, 2, 102.42903178906215, 13.74476800051571, 21.018874301742752},
    };
    for (const auto& x : rows) {
        expectRel(x.fwd, quanto_option::quantoForward(x.s, x.rf, x.q, x.ss, x.sx, x.rho, x.t), 1e-13);
        expectRel(x.call, quanto_option::price(OptionType::CALL, x.s, x.k, x.rd, x.rf, x.q, x.ss,
                                               x.sx, x.rho, x.t), 1e-12);
        expectRel(x.put, quanto_option::price(OptionType::PUT, x.s, x.k, x.rd, x.rf, x.q, x.ss,
                                              x.sx, x.rho, x.t), 1e-12);
    }
    EXPECT_THROWS(quanto_option::price(OptionType::CALL, 100, 100, 0.05, kNaN, 0, 0.2, 0.1, 0, 1));
}

// ---------------------------------------------------------------- C13

TEST(frtbStressedCalibrationIsReducedStressedTimesFullOverReduced) {
    // MAR33.6 hand values. Review case: 70/80/160 -> 160 * max(1, 70/80).
    EXPECT_NEAR(160, frtb_es::stressCalibratedEs(70, 160, 80), 1e-12);
    // ES_R,S * ES_F,C / ES_R,C = 150 * 100/80.
    EXPECT_NEAR(187.5, frtb_es::stressCalibratedEs(100, 150, 80), 1e-12);
    // Exactly 75% coverage passes, just below throws.
    EXPECT_NEAR(200, frtb_es::stressCalibratedEs(100, 150, 75), 1e-12);
    EXPECT_THROWS(frtb_es::stressCalibratedEs(100, 150, 74.999));
    EXPECT_THROWS(frtb_es::stressCalibratedEs(kNaN, 150, 80));
}

// ---------------------------------------------------------------- C14

TEST(acerbiTascheFractionalTailWeight) {
    // n = 100, c = 99%: w = 1, so ES is the single worst loss, not an average with VaR.
    std::vector<double> l(100, 10.0);
    l[99] = 1000;
    EXPECT_NEAR(1000, var_engine::tail(l, 0.99).expectedShortfall, 1e-9);
    // n = 250, c = 97.5%: w = 6.25. Losses 1..250: (250+...+245 + 0.25*244)/6.25.
    std::vector<double> m(250);
    for (int i = 0; i < 250; i++) {
        m[i] = i + 1;
    }
    double want = (250 + 249 + 248 + 247 + 246 + 245 + 0.25 * 244) / 6.25;
    EXPECT_NEAR(want, frtb_es::es975(m), 1e-10);
    EXPECT_NEAR(244, tail_risk::of(m, 0.975).var, 0.0);  // L(ceil(243.75))
    // 1..1000: w = 25 despite 0.025*1000 not being exact in binary.
    std::vector<double> k(1000);
    for (int i = 0; i < 1000; i++) {
        k[i] = i + 1;
    }
    EXPECT_NEAR(988.0, frtb_es::es975(k), 1e-10);
    EXPECT_NEAR(975, tail_risk::of(k, 0.975).var, 0.0);
    EXPECT_THROWS(tail_risk::of({1.0, kNaN, 3.0}, 0.9));
    EXPECT_THROWS(tail_risk::of({}, 0.9));
}

TEST(riskMetricsAndVarEngineShareOneExpectedShortfall) {
    std::vector<double> losses;
    std::vector<double> returns;
    for (int i = 0; i < 137; i++) {
        double x = std::sin(i * 1.7) * 0.02 + 0.001 * (i % 7);
        losses.push_back(-x);
        returns.push_back(x);
    }
    EXPECT_NEAR(var_engine::tail(losses, 0.95).expectedShortfall,
                risk_metrics::conditionalVar(returns, 0.95), 1e-15);
}

// ---------------------------------------------------------------- H10

TEST(golden_bachelierAgainstQuantLib) {
    struct { double f, k, v, t, call, put; } rows[] = {
    {-0.002, 0.001, 0.006, 2, 0.002094531973380699, 0.005094531973380699},
    {0.01, -0.005, 0.008, 5, 0.01700813509989532, 0.002008135099895317},
    {0.0, 0.0, 0.005, 1, 0.0019947114020071634, 0.0019947114020071634},
    {-0.01, -0.012, 0.004, 0.5, 0.0023992824567484914, 0.0003992824567484915},
    {0.03, 0.08, 0.002, 1, 2.4375940925978737e-142, 0.05},
    };
    for (const auto& x : rows) {
        EXPECT_NEAR(x.call, bachelier::price(OptionType::CALL, x.f, x.k, 0, x.v, x.t), 1e-15);
        EXPECT_NEAR(x.put, bachelier::price(OptionType::PUT, x.f, x.k, 0, x.v, x.t), 1e-15);
        double p = bachelier::price(OptionType::CALL, x.f, x.k, 0, x.v, x.t);
        if (p > 1e-9) {
            EXPECT_NEAR(x.v, bachelier::impliedVol(OptionType::CALL, p, x.f, x.k, 0, x.t), 1e-10);
        }
    }
    EXPECT_NEAR(std::exp(-0.02 * 2) * 0.002094531973380699,
                bachelier::price(OptionType::CALL, -0.002, 0.001, 0.02, 0.006, 2), 1e-15);
    EXPECT_THROWS(bachelier::price(OptionType::CALL, kNaN, 0, 0, 0.01, 1));
    EXPECT_TRUE(std::isnan(bachelier::impliedVol(OptionType::CALL, -1, 0, 0, 0, 1)));
    // Delta is the bump of the price.
    double h = 1e-6;
    double fd = (bachelier::price(OptionType::PUT, -0.002 + h, 0.001, 0, 0.006, 2)
                 - bachelier::price(OptionType::PUT, -0.002 - h, 0.001, 0, 0.006, 2)) / (2 * h);
    EXPECT_NEAR(fd, bachelier::delta(OptionType::PUT, -0.002, 0.001, 0, 0.006, 2), 1e-8);
    double vfd = (bachelier::price(OptionType::CALL, -0.002, 0.001, 0, 0.006 + h, 2)
                  - bachelier::price(OptionType::CALL, -0.002, 0.001, 0, 0.006 - h, 2)) / (2 * h);
    EXPECT_NEAR(vfd, bachelier::vega(-0.002, 0.001, 0, 0.006, 2), 1e-8);
}

TEST(golden_shiftedLognormalAgainstQuantLib) {
    struct { double f, k, shift, v, t, call, put; } rows[] = {
    {-0.002, 0.001, 0.03, 0.2, 2, 0.002028539597729492, 0.005028539597729493},
    {0.01, -0.005, 0.02, 0.3, 5, 0.016072997612303574, 0.0010729976123035785},
    };
    for (const auto& x : rows) {
        rates_options::VolModel m = rates_options::VolModel::shiftedLognormal(x.shift);
        EXPECT_NEAR(x.call, m.forwardValue(OptionType::CALL, x.f, x.k, x.v, x.t), 1e-14);
        EXPECT_NEAR(x.put, m.forwardValue(OptionType::PUT, x.f, x.k, x.v, x.t), 1e-14);
    }
}

TEST(negativeRatesPriceOrFailLoudlyNeverNaN) {
    YieldCurve neg = flatCurve(-0.005);
    auto normal = rates_options::VolModel::normal();
    // Lognormal entry points refuse, instead of the old silent NaN.
    EXPECT_THROWS(rates_options::swaption(neg, 1, 5, 0.01, 0.2, true));
    EXPECT_THROWS(rates_options::cap(neg, 5, 0.01, 0.2));
    EXPECT_THROWS(black76::price(OptionType::CALL, -0.01, 0.01, 0, 0.2, 1));
    EXPECT_THROWS(rates_options::VolModel::lognormal().forwardValue(OptionType::CALL, -0.01, 0.01,
                                                                    0.2, 1));
    // Payer minus receiver is annuity * (F - K), negative rates included.
    double a = rates_options::annuity(neg, 1.0, 5.0, 2);
    double f = rates_options::forwardSwapRate(neg, 1.0, 5.0, 2);
    EXPECT_TRUE(f < 0);
    double k = -0.001;
    double payer = rates_options::swaption(neg, 1.0, 5.0, 2, k, 0.006, true, normal);
    double recv = rates_options::swaption(neg, 1.0, 5.0, 2, k, 0.006, false, normal);
    EXPECT_NEAR(a * (f - k), payer - recv, 1e-14);
    EXPECT_NEAR(0.006, rates_options::swaptionImpliedVol(neg, 1.0, 5.0, 2, k, payer, true, normal),
                1e-10);
    // Cap minus floor is the swap: sum tau*DF*(fwd - K).
    double cap = rates_options::capFloor(neg, 3.0, 4, k, 0.006, true, normal);
    double floor = rates_options::capFloor(neg, 3.0, 4, k, 0.006, false, normal);
    double swap = 0;
    for (int i = 1; i <= 12; i++) {
        double d0 = neg.discountFactor((i - 1) * 0.25), d1 = neg.discountFactor(i * 0.25);
        swap += 0.25 * d1 * ((d0 / d1 - 1) / 0.25 - k);
    }
    EXPECT_NEAR(swap, cap - floor, 1e-13);
    EXPECT_THROWS(rates_options::capFloor(neg, 3.1, 4, k, 0.006, true, normal));
    EXPECT_THROWS(rates_options::swaption(neg, 1.0, 5.0, 5, k, 0.006, true, normal));
}

// ---------------------------------------------------------------- H11

TEST(golden_dayCountsAgainstQuantLib) {
    struct { Ymd s, e; double a360, a365, bond, euro, isda, actIsda; } rows[] = {
    {{2024,1,15}, {2024,7,15}, 0.5055555555555555, 0.4986301369863014, 0.5, 0.5, 0.5, 0.4972677595628415},
    {{2023,12,31}, {2024,2,29}, 0.16666666666666666, 0.1643835616438356, 0.1638888888888889, 0.1638888888888889, 0.16666666666666666, 0.163941911819747},
    {{2024,2,29}, {2025,2,28}, 1.0138888888888888, 1.0, 0.9972222222222222, 0.9972222222222222, 1.0, 0.9977019237966914},
    {{2023,1,31}, {2023,3,31}, 0.1638888888888889, 0.16164383561643836, 0.16666666666666666, 0.16666666666666666, 0.16666666666666666, 0.16164383561643836},
    {{2022,8,30}, {2023,8,31}, 1.0166666666666666, 1.0027397260273974, 1.0, 1.0, 1.0, 1.0027397260273974},
    {{2023,6,15}, {2026,3,1}, 2.75, 2.712328767123288, 2.7111111111111112, 2.7111111111111112, 2.7111111111111112, 2.7095890410958905},
    };
    for (const auto& x : rows) {
        LocalDate s = x.s.date(), e = x.e.date();
        EXPECT_NEAR(x.a360, day_count::yearFraction(DayCount::ACT_360, s, e), 1e-15);
        EXPECT_NEAR(x.a365, day_count::yearFraction(DayCount::ACT_365, s, e), 1e-15);
        EXPECT_NEAR(x.bond, day_count::yearFraction(DayCount::THIRTY_360, s, e), 1e-15);
        EXPECT_NEAR(x.euro, day_count::yearFraction(DayCount::THIRTY_E_360, s, e), 1e-15);
        EXPECT_NEAR(x.isda, day_count::yearFraction(DayCount::THIRTY_E_360_ISDA, s, e), 1e-15);
        EXPECT_NEAR(x.actIsda, day_count::yearFraction(DayCount::ACT_ACT_ISDA, s, e), 1e-15);
    }
    // February termination date keeps its day number (ql.Thirty360 ISDA).
    LocalDate s = LocalDate::of(2023, 8, 31), feb = LocalDate::of(2024, 2, 29);
    EXPECT_NEAR(0.49722222222222223,
                day_count::yearFraction(DayCount::THIRTY_E_360_ISDA, s, feb, feb), 1e-15);
    EXPECT_NEAR(0.5, day_count::yearFraction(DayCount::THIRTY_E_360_ISDA, s, feb), 1e-15);
    EXPECT_THROWS(day_count::yearFraction(DayCount::ACT_ACT_ICMA, s, feb));
    // A full regular period is exactly 1/frequency.
    EXPECT_NEAR(0.5, day_count::actActIcma(s, feb, s, feb, 2), 1e-15);
    // Mon 2026-11-02 to Mon 2026-11-09: five weekdays.
    EXPECT_NEAR(5 / 252.0, day_count::bus252(BusinessCalendar::weekendsOnly(),
                LocalDate::of(2026, 11, 2), LocalDate::of(2026, 11, 9)), 1e-15);
}

TEST(golden_scheduleAnchorsOnMaturityWithoutDrift) {
    using BC = BusinessCalendar;
    auto check = [](const std::vector<LocalDate>& got, std::vector<Ymd> want) {
        EXPECT_TRUE(got.size() == want.size());
        for (std::size_t i = 0; i < got.size() && i < want.size(); i++) {
            EXPECT_TRUE(got[i] == want[i].date());
        }
    };
    // ql.Schedule, Backward, no EOM. The old stepping gave 28 Aug after a February clamp.
    check(BC::unadjustedSchedule(LocalDate::of(2024, 2, 29), LocalDate::of(2027, 8, 31), 2),
          {{2024, 8, 31}, {2025, 2, 28}, {2025, 8, 31}, {2026, 2, 28}, {2026, 8, 31},
           {2027, 2, 28}, {2027, 8, 31}});
    check(BC::unadjustedSchedule(LocalDate::of(2025, 3, 10), LocalDate::of(2027, 8, 31), 2),
          {{2025, 8, 31}, {2026, 2, 28}, {2026, 8, 31}, {2027, 2, 28}, {2027, 8, 31}});
    // ql.Schedule, Backward, EOM.
    check(BC::unadjustedSchedule(LocalDate::of(2024, 2, 29), LocalDate::of(2027, 2, 28), 2,
                                 BC::StubType::SHORT_FRONT, true),
          {{2024, 8, 31}, {2025, 2, 28}, {2025, 8, 31}, {2026, 2, 28}, {2026, 8, 31},
           {2027, 2, 28}});
    check(BC::unadjustedSchedule(LocalDate::of(2024, 1, 31), LocalDate::of(2024, 12, 31), 4,
                                 BC::StubType::SHORT_FRONT, true),
          {{2024, 3, 31}, {2024, 6, 30}, {2024, 9, 30}, {2024, 12, 31}});
    // Stub placement, by hand: Jan 15 to Dec 1 2026 quarterly.
    LocalDate a = LocalDate::of(2026, 1, 15), b = LocalDate::of(2026, 12, 1);
    check(BC::unadjustedSchedule(a, b, 4, BC::StubType::LONG_FRONT, false),
          {{2026, 6, 1}, {2026, 9, 1}, {2026, 12, 1}});
    check(BC::unadjustedSchedule(a, b, 4, BC::StubType::SHORT_BACK, false),
          {{2026, 4, 15}, {2026, 7, 15}, {2026, 10, 15}, {2026, 12, 1}});
    check(BC::unadjustedSchedule(a, b, 4, BC::StubType::LONG_BACK, false),
          {{2026, 4, 15}, {2026, 7, 15}, {2026, 12, 1}});
}

TEST(golden_datedBondAgainstQuantLib) {
    struct { Ymd s, m; double c, y; int f; DayCount dc; bool eom; double clean, accrued; } rows[] = {
    {{2026,3,1}, {2031,8,15}, 0.05, 0.04, 2, DayCount::ACT_ACT_ICMA, false, 104.86084169010044, 0.1933701657458542},
    {{2026,3,1}, {2031,8,15}, 0.05, 0.04, 2, DayCount::THIRTY_360, false, 104.8560013207079, 0.22222222222221255},
    {{2026,5,20}, {2028,11,15}, 0.03, 0.045, 2, DayCount::ACT_ACT_ICMA, false, 96.5080089616101, 0.040760869565215074},
    {{2026,5,20}, {2028,11,15}, 0.03, 0.045, 2, DayCount::THIRTY_360, false, 96.50840043706495, 0.04166666666667318},
    };
    for (const auto& x : rows) {
        LocalDate s = x.s.date(), m = x.m.date();
        EXPECT_NEAR(x.accrued, bond_pricer::accruedInterest(100, x.c, x.f, s, m, x.dc, x.eom), 1e-10);
        EXPECT_NEAR(x.clean, bond_pricer::cleanPrice(100, x.c, x.f, s, m, x.y, x.dc, x.eom), 1e-9);
        EXPECT_NEAR(x.y, bond_pricer::yieldFromCleanPrice(x.clean, 100, x.c, x.f, s, m, x.dc, x.eom),
                    1e-10);
    }
    EXPECT_THROWS(bond_pricer::cleanPrice(100, 0.05, 2, LocalDate::of(2030, 1, 1),
                                          LocalDate::of(2029, 1, 1), 0.04, DayCount::ACT_365));
    EXPECT_THROWS(bond_pricer::yieldFromCleanPrice(1e9, 100, 0.05, 2, LocalDate::of(2026, 1, 1),
                                                   LocalDate::of(2029, 1, 1), DayCount::ACT_365));
}

TEST(bondStubIsPricedNotRounded) {
    // 6% semi, 1.25y at 8%: flows at 0.5, 1.5, 2.5 periods, mpmath 99.15034...
    // The old rounding to 3 whole periods gave 97.22.
    EXPECT_NEAR(99.1503416057714, bond_pricer::priceFromYield(100, 0.06, 2, 1.25, 0.08), 1e-10);
    EXPECT_NEAR(1.5, bond_pricer::accruedInterest(100, 0.06, 2, 1.25), 1e-12);
    EXPECT_NEAR(0.0, bond_pricer::accruedInterest(100, 0.06, 2, 3.0), 1e-12);
    // Curve pricing puts the same flows at 0.25, 0.75, 1.25 years.
    YieldCurve c = flatCurve(0.03);
    double want = 3 * c.discountFactor(0.25) + 3 * c.discountFactor(0.75)
            + 103 * c.discountFactor(1.25);
    EXPECT_NEAR(want, bond_pricer::priceFromCurve(100, 0.06, 2, 1.25, c), 1e-11);
    // YTM round-trips across the stub, and Macaulay duration is the weighted time.
    double p = bond_pricer::priceFromYield(100, 0.06, 2, 1.25, 0.08);
    EXPECT_NEAR(0.08, bond_pricer::yieldToMaturity(p, 100, 0.06, 2, 1.25), 1e-10);
    double pv1 = 3 / std::pow(1.04, 0.5), pv2 = 3 / std::pow(1.04, 1.5), pv3 = 103 / std::pow(1.04, 2.5);
    EXPECT_NEAR((0.25 * pv1 + 0.75 * pv2 + 1.25 * pv3) / (pv1 + pv2 + pv3),
                bond_pricer::macaulayDuration(100, 0.06, 2, 1.25, 0.08), 1e-12);
    EXPECT_THROWS(bond_pricer::priceFromYield(100, 0.06, 2, kNaN, 0.08));
}

TEST(cdsMaturityOffTheGridIsNotTruncated) {
    YieldCurve disc = flatCurve(0.03);
    CreditCurve credit = CreditCurve::bootstrap({1, 3, 5}, {0.01, 0.012, 0.015}, 0.4, disc);
    double dt = 0.25;
    // By hand on the stated discretisation: four quarters and a 0.24 stub.
    double a = 0, pv = 0, prevQ = 1;
    for (double t : {0.25, 0.5, 0.75, 1.0, 1.24}) {
        double tau = t == 1.24 ? 1.24 - 1.0 : dt;
        double q = credit.survivalProbability(t), df = disc.discountFactor(t);
        a += tau * df * q + 0.5 * tau * df * (prevQ - q);
        pv += df * (prevQ - q);
        prevQ = q;
    }
    EXPECT_NEAR(a, cds_pricer::riskyAnnuity(credit, disc, 1.24), 1e-13);
    EXPECT_NEAR(0.6 * pv, cds_pricer::protectionLegPv(credit, disc, 1.24), 1e-13);
    EXPECT_TRUE(cds_pricer::riskyAnnuity(credit, disc, 1.24)
                > cds_pricer::riskyAnnuity(credit, disc, 1.0) + 0.2);
}

// ---------------------------------------------------------------- H12

TEST(calendarArbitrageIsReported) {
    VolSurface bad = VolSurface::builder().add(0.5, 100, 0.30).add(1.0, 100, 0.20).build();
    auto v = bad.calendarViolations(5);
    EXPECT_TRUE(!v.empty());
    EXPECT_NEAR(0.045, v[0].totalVariance1, 1e-15);
    EXPECT_NEAR(0.04, v[0].totalVariance2, 1e-15);
    VolSurface good = VolSurface::builder().add(0.5, 100, 0.20).add(1.0, 100, 0.20).build();
    EXPECT_TRUE(good.calendarViolations(5).empty());
    // Equal strikes look fine; forwards reveal the violation at equal moneyness.
    VolSurface fwd = VolSurface::builder()
            .add(0.5, 90, 0.40).add(0.5, 110, 0.20)
            .add(1.0, 90, 0.40).add(1.0, 110, 0.20)
            .forward(0.5, 90).forward(1.0, 110).build();
    EXPECT_TRUE(!fwd.calendarViolations(9).empty());
    EXPECT_THROWS(VolSurface::builder().add(0.5, 100, 0.2).add(1.0, 100, 0.2)
                          .forward(0.5, 100).build());
    EXPECT_THROWS(good.calendarViolations(1));
}

TEST(butterflyArbitrageIsMeasured) {
    // A flat smile has the positive lognormal density at the money.
    VolSurface flat = VolSurface::builder().add(1, 80, 0.2).add(1, 120, 0.2).build();
    EXPECT_TRUE(flat.minimumDensity(1, 100, 21) > 0);
    // A sharp spike in the middle of the smile makes the call curve non-convex.
    VolSurface spike = VolSurface::builder()
            .add(1, 95, 0.20).add(1, 100, 0.60).add(1, 105, 0.20).build();
    EXPECT_TRUE(spike.minimumDensity(1, 100, 41) < 0);
    EXPECT_THROWS(flat.minimumDensity(2, 100, 21));
    // Hagan SABR at f=3%, T=10, alpha=.05, beta=.5, rho=-.3, nu=.5: QuantLib's
    // sabrVolatility gives vol(5%) = 0.29191 and a density of -124.6 at K = 0.1%.
    expectRel(0.2919149060214614, sabr_model::impliedVol(0.03, 0.05, 10, 0.05, 0.5, -0.3, 0.5), 1e-12);
    EXPECT_TRUE(sabr_model::minimumDensity(0.03, 10, 0.05, 0.5, -0.3, 0.5, 0.001, 0.1, 200) < -100);
}

TEST(zeroVolGreeksAreTheDeterministicLimits) {
    // S=100, K=90, r=5%, q=1%, T=1: in the money forward.
    double e1 = std::exp(-0.01), e5 = std::exp(-0.05);
    using namespace black_scholes;
    EXPECT_NEAR(e1, delta(OptionType::CALL, 100, 90, 0.05, 0.01, 0, 1), 1e-15);
    EXPECT_NEAR(0, delta(OptionType::PUT, 100, 90, 0.05, 0.01, 0, 1), 1e-15);
    EXPECT_NEAR(0, gamma(100, 90, 0.05, 0.01, 0, 1), 0.0);
    EXPECT_NEAR(0, vega(100, 90, 0.05, 0.01, 0, 1), 0.0);
    EXPECT_NEAR(0.01 * 100 * e1 - 0.05 * 90 * e5, theta(OptionType::CALL, 100, 90, 0.05, 0.01, 0, 1), 1e-12);
    EXPECT_NEAR(90 * e5, rho(OptionType::CALL, 100, 90, 0.05, 0.01, 0, 1), 1e-12);
    // Out of the money put side of the same limit, and the ATM-forward half.
    EXPECT_NEAR(-e1, delta(OptionType::PUT, 100, 120, 0.05, 0.01, 0, 1), 1e-15);
    EXPECT_NEAR(0.5, delta(OptionType::CALL, 100, 100, 0, 0, 0, 1), 1e-15);
    EXPECT_NEAR(100 * 0.3989422804014327, vega(100, 100, 0, 0, 0, 1), 1e-12);
    // Greeks struct: no NaN anywhere at vol = 0.
    Greeks g = greeks(OptionType::CALL, 100, 100, 0, 0, 0, 1);
    EXPECT_TRUE(std::isfinite(g.delta) && std::isfinite(g.gamma) && std::isfinite(g.vega)
                && std::isfinite(g.theta) && std::isfinite(g.rho));
    EXPECT_TRUE(std::isnan(impliedVol(OptionType::CALL, kNaN, 100, 100, 0, 0, 1)));
}

TEST(golden_touchProbabilityNeverOverflows) {
    struct { double s, h, r, q, v, t, p; } rows[] = {
    {100, 90, 0.03, 0.01, 0.25, 0.75, 0.6383569801565222},
    {100, 105, 0.5, 0, 0.01, 1, 1.0},
    {100, 101, 0.3, 0, 0.001, 0.1, 1.0},
    {100, 95, -0.5, 0, 0.01, 1, 1.0},
    {100, 130, 0.05, 0, 0.002, 1, 0.0},
    {100, 101, 0.01, 0, 0.0005, 1, 0.5494091645786494},
    };
    for (const auto& x : rows) {
        EXPECT_NEAR(x.p, touch_option::hitProbability(x.s, x.h, x.r, x.q, x.v, x.t), 1e-12);
    }
    EXPECT_THROWS(touch_option::hitProbability(kNaN, 100, 0, 0, 0.2, 1));
    EXPECT_THROWS(touch_option::hitProbability(100, 90, 0, 0, kNaN, 1));
}

TEST(choleskyAgreesAcrossEntryPoints) {
    using M = math_utils::Matrix;
    M spd = {{4, 2}, {2, 3}};
    M a = math_utils::cholesky(spd), b = GaussianCopula::cholesky(spd);
    EXPECT_NEAR(a[1][1], b[1][1], 0.0);
    EXPECT_NEAR(std::sqrt(2.0), a[1][1], 1e-15);
    // Rank-deficient: the tolerant form jitters relative to scale, the strict one throws.
    double s = 2.5e-9;
    M singular = {{s, s}, {s, s}};
    M l = math_utils::cholesky(singular);
    EXPECT_NEAR(std::sqrt(1e-12 * s), l[1][1], 1e-18);
    EXPECT_THROWS(GaussianCopula::cholesky(singular));
    EXPECT_THROWS(math_utils::cholesky({{1, 2}, {2, 1}}));
    EXPECT_THROWS(math_utils::cholesky({{1, 0}}));
    EXPECT_THROWS(math_utils::cholesky({{1, kNaN}, {kNaN, 1}}));
    // Monte Carlo VaR accepts the same perfectly correlated pair.
    var_engine::VarResult r = var_engine::monteCarloVar({1, 1}, {{0.04, 0.04}, {0.04, 0.04}}, 0.99,
                                                        20000, 7);
    EXPECT_NEAR(2 * 0.2 * 2.3263478740408408, r.var, 0.06);
}

// ---------------------------------------------------------------- barriers

TEST(golden_allEightBarriersAgainstQuantLib) {
    // S=100, r=8%, q=4%, vol=25%, T=1.
    struct { OptionType t; Direction d; Knock k; double strike, h, rebate, price; } rows[] = {
    {OptionType::CALL, Direction::DOWN, Knock::IN, 90, 95, 0, 10.229527620472304},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 90, 95, 3, 10.696526688014048},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 90, 85, 0, 2.4996796394268106},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 90, 85, 3, 3.873491071640684},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 110, 95, 0, 3.688113352001057},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 110, 95, 3, 4.155112419542801},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 110, 85, 0, 0.5565238147790712},
    {OptionType::CALL, Direction::DOWN, Knock::IN, 110, 85, 3, 1.9303352469929447},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 90, 95, 0, 6.635233733716632},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 90, 95, 3, 9.100036298157553},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 90, 85, 0, 14.365081714762125},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 90, 85, 3, 15.830507963103669},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 110, 95, 0, 3.647577557242709},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 110, 95, 3, 6.11238012168363},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 110, 85, 0, 6.779167094464695},
    {OptionType::CALL, Direction::DOWN, Knock::OUT, 110, 85, 3, 8.244593342806239},
    {OptionType::CALL, Direction::UP, Knock::IN, 90, 105, 0, 16.7432266198476},
    {OptionType::CALL, Direction::UP, Knock::IN, 90, 105, 3, 17.15589759730066},
    {OptionType::CALL, Direction::UP, Knock::IN, 90, 120, 0, 14.676901231418883},
    {OptionType::CALL, Direction::UP, Knock::IN, 90, 120, 3, 16.1232067127448},
    {OptionType::CALL, Direction::UP, Knock::IN, 110, 105, 0, 7.335690909243766},
    {OptionType::CALL, Direction::UP, Knock::IN, 110, 105, 3, 7.748361886696827},
    {OptionType::CALL, Direction::UP, Knock::IN, 110, 120, 0, 7.254035679382703},
    {OptionType::CALL, Direction::UP, Knock::IN, 110, 120, 3, 8.700341160708618},
    {OptionType::CALL, Direction::UP, Knock::OUT, 90, 105, 0, 0.12153473434133488},
    {OptionType::CALL, Direction::UP, Knock::OUT, 90, 105, 3, 2.645831984847585},
    {OptionType::CALL, Direction::UP, Knock::OUT, 90, 120, 0, 2.1878601227700525},
    {OptionType::CALL, Direction::UP, Knock::OUT, 90, 120, 3, 3.5733986718133695},
    {OptionType::CALL, Direction::UP, Knock::OUT, 110, 105, 0, 0.0},
    {OptionType::CALL, Direction::UP, Knock::OUT, 110, 105, 3, 2.52429725050625},
    {OptionType::CALL, Direction::UP, Knock::OUT, 110, 120, 0, 0.0816552298610631},
    {OptionType::CALL, Direction::UP, Knock::OUT, 110, 120, 3, 1.4671937789043799},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 90, 95, 0, 3.866288613753845},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 90, 95, 3, 4.333287681295589},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 90, 85, 0, 3.849603664308395},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 90, 85, 3, 5.2234150965222685},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 110, 95, 0, 12.673874156070344},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 110, 95, 3, 13.140873223612088},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 110, 85, 0, 11.210031885967547},
    {OptionType::PUT, Direction::DOWN, Knock::IN, 110, 85, 3, 12.58384331818142},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 90, 95, 0, 0.0},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 90, 95, 3, 2.4648025644409213},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 90, 85, 0, 0.01668494944544996},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 90, 85, 3, 1.4821111977869936},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 110, 95, 0, 0.12567094047102856},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 110, 95, 3, 2.59047350491195},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 110, 85, 0, 1.5895132105738252},
    {OptionType::PUT, Direction::DOWN, Knock::OUT, 110, 85, 3, 3.054939458915369},
    {OptionType::PUT, Direction::UP, Knock::IN, 90, 105, 0, 2.0794383585725633},
    {OptionType::PUT, Direction::UP, Knock::IN, 90, 105, 3, 2.4921093360256243},
    {OptionType::PUT, Direction::UP, Knock::IN, 90, 120, 0, 0.21368237468272433},
    {OptionType::PUT, Direction::UP, Knock::IN, 90, 120, 3, 1.6599878560086396},
    {OptionType::PUT, Direction::UP, Knock::IN, 110, 105, 0, 8.383089726014347},
    {OptionType::PUT, Direction::UP, Knock::IN, 110, 105, 3, 8.795760703467408},
    {OptionType::PUT, Direction::UP, Knock::IN, 110, 120, 0, 1.6111072082064872},
    {OptionType::PUT, Direction::UP, Knock::IN, 110, 120, 3, 3.0574126895324025},
    {OptionType::PUT, Direction::UP, Knock::OUT, 90, 105, 0, 1.7868502551812817},
    {OptionType::PUT, Direction::UP, Knock::OUT, 90, 105, 3, 4.311147505687532},
    {OptionType::PUT, Direction::UP, Knock::OUT, 90, 120, 0, 3.6526062390711207},
    {OptionType::PUT, Direction::UP, Knock::OUT, 90, 120, 3, 5.038144788114438},
    {OptionType::PUT, Direction::UP, Knock::OUT, 110, 105, 0, 4.416455370527025},
    {OptionType::PUT, Direction::UP, Knock::OUT, 110, 105, 3, 6.940752621033275},
    {OptionType::PUT, Direction::UP, Knock::OUT, 110, 120, 0, 11.188437888334885},
    {OptionType::PUT, Direction::UP, Knock::OUT, 110, 120, 3, 12.573976437378201},
    };
    for (const auto& x : rows) {
        EXPECT_NEAR(x.price, barrier_option::price(x.t, x.d, x.k, 100, x.strike, x.h, x.rebate,
                                                   0.08, 0.04, 0.25, 1.0), 1e-10);
    }
    // Breached: knock-in is the vanilla, knock-out the rebate.
    double vanilla = black_scholes::price(OptionType::CALL, 100, 90, 0.08, 0.04, 0.25, 1);
    EXPECT_NEAR(vanilla, barrier_option::price(OptionType::CALL, Direction::DOWN, Knock::IN, 100,
                                               90, 100, 3, 0.08, 0.04, 0.25, 1), 0.0);
    EXPECT_NEAR(3, barrier_option::price(OptionType::CALL, Direction::UP, Knock::OUT, 100, 90, 95,
                                         3, 0.08, 0.04, 0.25, 1), 0.0);
    // Expired and alive.
    EXPECT_NEAR(10, barrier_option::price(OptionType::CALL, Direction::UP, Knock::OUT, 100, 90, 120,
                                          3, 0.08, 0.04, 0.25, 0), 0.0);
    EXPECT_NEAR(3, barrier_option::price(OptionType::CALL, Direction::UP, Knock::IN, 100, 90, 120,
                                         3, 0.08, 0.04, 0.25, 0), 0.0);
    EXPECT_THROWS(barrier_option::price(OptionType::CALL, Direction::UP, Knock::IN, 100, 90, 120, 0,
                                        0.08, 0.04, 0, 1));
    EXPECT_THROWS(barrier_option::price(OptionType::CALL, Direction::UP, Knock::IN, kNaN, 90, 120, 0,
                                        0.08, 0.04, 0.25, 1));
    // Low vol with a large exponent: finite, not inf * 0.
    double p = barrier_option::price(OptionType::CALL, Direction::UP, Knock::OUT, 100, 90, 130, 0,
                                     0.05, 0, 0.01, 1);
    EXPECT_TRUE(std::isfinite(p));
}

// ---------------------------------------------------------------- H13

TEST(ewmaIsCausal) {
    std::vector<double> r = {0.01, -0.02, 0.005, 0.03, -0.01};
    EwmaVolatility e(0.9);
    std::vector<double> h = e.variances(r);
    // Changing a later return must leave every earlier estimate alone. The old
    // full-sample seed moved all of them.
    std::vector<double> r2 = r;
    r2[4] = 5.0;
    std::vector<double> h2 = e.variances(r2);
    for (std::size_t i = 0; i < r.size(); i++) {
        EXPECT_NEAR(h[i], h2[i], 0.0);
    }
    // Bias-corrected weights by hand at t = 2: (0.1*0.9*r0^2 + 0.1*r1^2)/(1 - 0.81).
    EXPECT_NEAR((0.09 * 1e-4 + 0.1 * 4e-4) / 0.19, h[2], 1e-16);
    EXPECT_NEAR(1e-4, h[1], 1e-16);
    EXPECT_NEAR(h[1], h[0], 0.0);
    // Forecast continues the same weights one step further.
    double raw = 0, mass = 0;
    for (double x : r) {
        raw = 0.9 * raw + 0.1 * x * x;
        mass = 0.9 * mass + 0.1;
    }
    EXPECT_NEAR(std::sqrt(raw / mass), e.latestVol(r), 1e-15);
    // Explicit prior.
    std::vector<double> p = e.variances(r, 2e-4);
    EXPECT_NEAR(2e-4, p[0], 0.0);
    EXPECT_NEAR(0.9 * 2e-4 + 0.1 * 1e-4, p[1], 1e-16);
    EXPECT_THROWS(e.variances(r, 0.0));
    EXPECT_THROWS(e.variances({0.01, kNaN}));
}

QTEST_MAIN()
