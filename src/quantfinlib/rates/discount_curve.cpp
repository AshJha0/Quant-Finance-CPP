#include "quantfinlib/rates/discount_curve.hpp"

#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {

namespace {

void requireTime(double time) {
    if (!(time >= 0) || !std::isfinite(time)) {
        throw std::invalid_argument("time must be finite and >= 0, got " + std::to_string(time));
    }
}

// Hagan-West g(x) on one interval, or its integral over [0, x].
double shape(double g0, double g1, double x, bool integral) {
    if (x <= 0) {
        return integral ? 0 : g0;
    }
    if (g0 == 0 && g1 == 0) {
        return 0;
    }
    if ((g0 < 0 && -0.5 * g0 <= g1 && g1 <= -2 * g0)
            || (g0 > 0 && -0.5 * g0 >= g1 && g1 >= -2 * g0)) {
        // zone (i): one quadratic
        return integral ? g0 * (x - 2 * x * x + x * x * x) + g1 * (-x * x + x * x * x)
                        : g0 * (1 - 4 * x + 3 * x * x) + g1 * (-2 * x + 3 * x * x);
    }
    if ((g0 < 0 && g1 > -2 * g0) || (g0 > 0 && g1 < -2 * g0)) {
        // zone (ii): flat at g0, then quadratic from eta
        double eta = (g1 + 2 * g0) / (g1 - g0);
        if (x <= eta) {
            return integral ? g0 * x : g0;
        }
        double u = (x - eta) / (1 - eta);
        return integral ? g0 * x + (g1 - g0) * u * u * (x - eta) / 3 : g0 + (g1 - g0) * u * u;
    }
    if ((g0 > 0 && g1 < 0 && g1 > -0.5 * g0) || (g0 < 0 && g1 > 0 && g1 < -0.5 * g0)) {
        // zone (iii): quadratic until eta, then flat at g1
        double eta = 3 * g1 / (g1 - g0);
        if (x < eta) {
            double u = (eta - x) / eta;
            return integral ? g1 * x + (g0 - g1) / 3 * (eta - u * u * (eta - x))
                            : g1 + (g0 - g1) * u * u;
        }
        return integral ? g1 * x + (g0 - g1) * eta / 3 : g1;
    }
    // zone (iv): g0 and g1 of the same sign, two quadratics meeting at eta
    double eta = g1 / (g1 + g0);
    double a = -g0 * g1 / (g0 + g1);
    if (x <= eta) {
        double u = (eta - x) / eta;
        return integral ? a * x + (g0 - a) / 3 * (eta - u * u * (eta - x))
                        : a + (g0 - a) * u * u;
    }
    double u = (x - eta) / (1 - eta);
    return integral ? a * x + (g0 - a) * eta / 3 + (g1 - a) * u * u * (x - eta) / 3
                    : a + (g1 - a) * u * u;
}

}  // namespace

DiscountCurve::DiscountCurve(const LocalDate& valuationDate, CurveInterpolation interpolation,
                             const std::vector<double>& times,
                             const std::vector<double>& minusLogDf)
    : valuationDate_(valuationDate), interpolation_(interpolation) {
    if (times.empty() || times.size() != minusLogDf.size()) {
        throw std::invalid_argument("need equal-length, non-empty times and values");
    }
    std::size_t n = times.size();
    t_.assign(n + 1, 0.0);
    y_.assign(n + 1, 0.0);
    double prev = 0;
    for (std::size_t i = 0; i < n; i++) {
        if (!(times[i] > prev) || !std::isfinite(times[i]) || !std::isfinite(minusLogDf[i])) {
            throw std::invalid_argument("times must be finite, positive and strictly increasing");
        }
        prev = times[i];
        t_[i + 1] = times[i];
        y_[i + 1] = minusLogDf[i];
    }
    fd_.assign(n + 1, 0.0);
    for (std::size_t i = 1; i <= n; i++) {
        fd_[i] = (y_[i] - y_[i - 1]) / (t_[i] - t_[i - 1]);
    }
    fn_.assign(n + 1, 0.0);
    if (interpolation == CurveInterpolation::MONOTONE_CONVEX) {
        if (n == 1) {
            fn_[0] = fn_[1] = fd_[1];
        } else {
            for (std::size_t i = 1; i < n; i++) {
                double w = t_[i + 1] - t_[i - 1];
                fn_[i] = (t_[i] - t_[i - 1]) / w * fd_[i + 1] + (t_[i + 1] - t_[i]) / w * fd_[i];
            }
            fn_[0] = fd_[1] - 0.5 * (fn_[1] - fd_[1]);
            fn_[n] = fd_[n] - 0.5 * (fn_[n - 1] - fd_[n]);
        }
    }
}

DiscountCurve DiscountCurve::of(const LocalDate& valuationDate, const std::vector<double>& times,
                                const std::vector<double>& discountFactors,
                                CurveInterpolation interpolation) {
    std::vector<double> y(discountFactors.size());
    for (std::size_t i = 0; i < y.size(); i++) {
        if (!(discountFactors[i] > 0) || !std::isfinite(discountFactors[i])) {
            throw std::invalid_argument("discount factors must be positive and finite");
        }
        y[i] = -std::log(discountFactors[i]);
    }
    return DiscountCurve(valuationDate, interpolation, times, y);
}

DiscountCurve DiscountCurve::ofMinusLogDf(const LocalDate& valuationDate,
                                          const std::vector<double>& times,
                                          const std::vector<double>& minusLogDf,
                                          CurveInterpolation interpolation) {
    return DiscountCurve(valuationDate, interpolation, times, minusLogDf);
}

double DiscountCurve::time(const LocalDate& date) const {
    return day_count::yearFraction(DayCount::ACT_365, valuationDate_, date);
}

double DiscountCurve::discountFactor(double time) const {
    return std::exp(-minusLogDf(time));
}

double DiscountCurve::discountFactor(const LocalDate& date) const {
    return discountFactor(time(date));
}

double DiscountCurve::zeroRate(double time) const {
    return time == 0 ? instantaneousForward(0) : minusLogDf(time) / time;
}

double DiscountCurve::forwardRate(const LocalDate& start, const LocalDate& end,
                                  DayCount dayCount) const {
    double tau = day_count::yearFraction(dayCount, start, end);
    if (!(tau > 0)) {
        throw std::invalid_argument("end must be after start");
    }
    return std::expm1(minusLogDf(time(end)) - minusLogDf(time(start))) / tau;
}

int DiscountCurve::interval(double time) const {
    int lo = 1;
    int hi = static_cast<int>(t_.size()) - 1;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (t_[mid] >= time) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo;
}

double DiscountCurve::instantaneousForward(double time) const {
    requireTime(time);
    int n = static_cast<int>(t_.size()) - 1;
    bool mc = interpolation_ == CurveInterpolation::MONOTONE_CONVEX;
    if (time >= t_[n]) {
        return mc ? fn_[n] : fd_[n];
    }
    int i = interval(time);
    if (!mc) {
        return time == t_[i] ? fd_[i + 1] : fd_[i];
    }
    double x = (time - t_[i - 1]) / (t_[i] - t_[i - 1]);
    return fd_[i] + shape(fn_[i - 1] - fd_[i], fn_[i] - fd_[i], x, false);
}

double DiscountCurve::minusLogDf(double time) const {
    requireTime(time);
    if (time == 0) {
        return 0;
    }
    int n = static_cast<int>(t_.size()) - 1;
    bool mc = interpolation_ == CurveInterpolation::MONOTONE_CONVEX;
    if (time >= t_[n]) {
        return y_[n] + (mc ? fn_[n] : fd_[n]) * (time - t_[n]);
    }
    int i = interval(time);
    double dt = time - t_[i - 1];
    if (!mc) {
        return y_[i - 1] + fd_[i] * dt;
    }
    double h = t_[i] - t_[i - 1];
    return y_[i - 1] + fd_[i] * dt + h * shape(fn_[i - 1] - fd_[i], fn_[i] - fd_[i], dt / h, true);
}

DiscountCurve DiscountCurve::parallelShift(double shift) const {
    if (!std::isfinite(shift)) {
        throw std::invalid_argument("shift must be finite");
    }
    std::vector<double> times(t_.begin() + 1, t_.end());
    std::vector<double> y(y_.begin() + 1, y_.end());
    for (std::size_t i = 0; i < y.size(); i++) {
        y[i] += shift * times[i];
    }
    return DiscountCurve(valuationDate_, interpolation_, times, y);
}

}  // namespace quantfinlib
