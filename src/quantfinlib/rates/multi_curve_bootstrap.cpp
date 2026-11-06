#include "quantfinlib/rates/multi_curve_bootstrap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace multi_curve_bootstrap {

namespace {

constexpr int kMaxIter = 50;
constexpr double kFdStep = 1e-7;
using Vec = std::vector<double>;

double maxAbs(const Vec& v) {
    double m = 0;
    for (double x : v) {
        if (std::isnan(x)) {
            return std::numeric_limits<double>::infinity();
        }
        m = std::max(m, std::fabs(x));
    }
    return m;
}

DiscountCurve solve(const LocalDate& val, const std::vector<CurveInstrumentPtr>& input,
                    CurveInterpolation interp, const DiscountCurve* discount) {
    if (input.empty()) {
        throw std::invalid_argument("need at least one instrument");
    }
    std::vector<CurveInstrumentPtr> inst = input;
    std::stable_sort(inst.begin(), inst.end(), [](const auto& a, const auto& b) {
        return a->pillarDate() < b->pillarDate();
    });
    int n = static_cast<int>(inst.size());
    Vec times(n);
    for (int i = 0; i < n; i++) {
        times[i] = day_count::yearFraction(DayCount::ACT_365, val, inst[i]->pillarDate());
        if (!(times[i] > (i == 0 ? 0 : times[i - 1]))) {
            throw std::invalid_argument("pillar dates must be distinct and after the valuation "
                                        "date: " + inst[i]->pillarDate().toString());
        }
    }
    auto residual = [&](const CurveInstrument& c, const Vec& t, const Vec& y) {
        DiscountCurve curve = DiscountCurve::ofMinusLogDf(val, t, y, interp);
        return c.impliedQuote(discount != nullptr ? *discount : curve, curve) - c.quote();
    };
    auto residuals = [&](const Vec& y) {
        DiscountCurve curve = DiscountCurve::ofMinusLogDf(val, times, y, interp);
        const DiscountCurve& d = discount != nullptr ? *discount : curve;
        Vec r(n);
        for (int i = 0; i < n; i++) {
            r[i] = inst[i]->impliedQuote(d, curve) - inst[i]->quote();
        }
        return r;
    };
    Vec y(n, 0.0);
    for (int k = 0; k < n; k++) {
        Vec tk(times.begin(), times.begin() + k + 1);
        Vec yk(y.begin(), y.begin() + k + 1);
        yk[k] = k == 0 ? 0.02 * tk[0] : yk[k - 1] * tk[k] / tk[k - 1];
        for (int it = 0; it < kMaxIter; it++) {
            double r = residual(*inst[k], tk, yk);
            if (std::fabs(r) <= TOLERANCE * 1e-2) {
                break;
            }
            double y0 = yk[k];
            yk[k] = y0 + kFdStep;
            double d = (residual(*inst[k], tk, yk) - r) / kFdStep;
            if (!(std::fabs(d) > 0) || !std::isfinite(d)) {
                throw std::runtime_error("instrument insensitive to its pillar");
            }
            yk[k] = y0 - r / d;
        }
        y[k] = yk[k];
    }
    Vec r = residuals(y);
    for (int it = 0; it < kMaxIter && maxAbs(r) > TOLERANCE * 1e-2; it++) {
        math_utils::Matrix jac(n, Vec(n));
        for (int j = 0; j < n; j++) {
            double yj = y[j];
            y[j] = yj + kFdStep;
            Vec rb = residuals(y);
            y[j] = yj;
            for (int i = 0; i < n; i++) {
                jac[i][j] = (rb[i] - r[i]) / kFdStep;
            }
        }
        Vec step = math_utils::solveLinear(jac, r);
        Vec trial = y;
        for (int i = 0; i < n; i++) {
            trial[i] -= step[i];
        }
        Vec rNew = residuals(trial);
        if (!(maxAbs(rNew) < maxAbs(r))) {
            break;
        }
        y = trial;
        r = rNew;
    }
    if (!(maxAbs(r) <= TOLERANCE)) {
        throw std::runtime_error("bootstrap did not converge, max residual "
                                 + std::to_string(maxAbs(r)));
    }
    return DiscountCurve::ofMinusLogDf(val, times, y, interp);
}

}  // namespace

DiscountCurve bootstrap(const LocalDate& valuationDate,
                        const std::vector<CurveInstrumentPtr>& instruments,
                        CurveInterpolation interpolation) {
    return solve(valuationDate, instruments, interpolation, nullptr);
}

DiscountCurve bootstrapProjection(const LocalDate& valuationDate,
                                  const std::vector<CurveInstrumentPtr>& instruments,
                                  CurveInterpolation interpolation,
                                  const DiscountCurve& discount) {
    if (discount.valuationDate() != valuationDate) {
        throw std::invalid_argument("discount curve has a different valuation date");
    }
    for (const auto& c : instruments) {
        if (c->discountOnly()) {
            throw std::invalid_argument("an OisSwap does not depend on a projection curve");
        }
    }
    return solve(valuationDate, instruments, interpolation, &discount);
}

}  // namespace multi_curve_bootstrap
}  // namespace quantfinlib
