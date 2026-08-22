#include "quantfinlib/fx/fx_vol_surface.hpp"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

double paCallDelta(double forward, double strike, double sv) {
    return (strike / forward) * math_utils::normCdf(std::log(forward / strike) / sv - 0.5 * sv);
}

double d2Of(double forward, double strike, double sv) {
    return std::log(forward / strike) / sv - 0.5 * sv;
}

/** Bisection for a function decreasing over [lo, hi] with a sign change. */
double bisect(const std::function<double(double)>& f, double lo, double hi) {
    for (int i = 0; i < 200; i++) {
        double mid = 0.5 * (lo + hi);
        if (f(mid) > 0) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return 0.5 * (lo + hi);
}

}  // namespace

FxVolSurface::FxVolSurface(std::vector<double> expiries, std::vector<double> forwards,
                           std::vector<std::vector<double>> logMoneyness,
                           std::vector<std::vector<double>> vols, bool premiumAdjusted)
    : expiries_(std::move(expiries)),
      forwards_(std::move(forwards)),
      logMoneyness_(std::move(logMoneyness)),
      vols_(std::move(vols)),
      premiumAdjusted_(premiumAdjusted) {}

FxVolSurface::Builder& FxVolSurface::Builder::add(double expiryYears, double forward,
                                                  double atmVol, double rr25, double bf25) {
    return add(expiryYears, forward, atmVol, rr25, bf25,
              std::numeric_limits<double>::quiet_NaN(),
              std::numeric_limits<double>::quiet_NaN());
}

FxVolSurface::Builder& FxVolSurface::Builder::add(double expiryYears, double forward,
                                                  double atmVol, double rr25, double bf25,
                                                  double rr10, double bf10) {
    if (expiryYears <= 0 || forward <= 0 || atmVol <= 0) {
        throw std::invalid_argument("expiry, forward and atm vol must be > 0");
    }
    bool tenDelta = !std::isnan(rr10);
    double v25c = atmVol + bf25 + rr25 / 2;
    double v25p = atmVol + bf25 - rr25 / 2;
    double v10c = tenDelta ? atmVol + bf10 + rr10 / 2 : std::numeric_limits<double>::quiet_NaN();
    double v10p = tenDelta ? atmVol + bf10 - rr10 / 2 : std::numeric_limits<double>::quiet_NaN();
    if (!std::isfinite(v25c) || v25c <= 0 || !std::isfinite(v25p) || v25p <= 0
        || (tenDelta && (!std::isfinite(v10c) || v10c <= 0 || !std::isfinite(v10p) || v10p <= 0))) {
        throw std::invalid_argument(
            "risk-reversal/butterfly combination produces a non-positive or non-finite wing vol");
    }
    quotes_.push_back({expiryYears, forward, atmVol, rr25, bf25, rr10, bf10});
    return *this;
}

FxVolSurface::Builder& FxVolSurface::Builder::premiumAdjusted(bool value) {
    premiumAdjusted_ = value;
    return *this;
}

FxVolSurface FxVolSurface::Builder::build() const {
    if (quotes_.empty()) {
        throw std::runtime_error("at least one expiry quote required");
    }
    std::vector<Quote> sorted = quotes_;
    std::stable_sort(sorted.begin(), sorted.end(),
                     [](const Quote& a, const Quote& b) { return a.t < b.t; });
    std::size_t n = sorted.size();
    std::vector<double> ts(n), fwds(n);
    std::vector<std::vector<double>> lm(n), vv(n);
    for (std::size_t i = 0; i < n; i++) {
        const Quote& q = sorted[i];
        ts[i] = q.t;
        fwds[i] = q.forward;
        bool tenDelta = !std::isnan(q.rr10);
        double v25c = q.atm + q.bf25 + q.rr25 / 2;
        double v25p = q.atm + q.bf25 - q.rr25 / 2;
        double v10c = tenDelta ? q.atm + q.bf10 + q.rr10 / 2 : std::numeric_limits<double>::quiet_NaN();
        double v10p = tenDelta ? q.atm + q.bf10 - q.rr10 / 2 : std::numeric_limits<double>::quiet_NaN();
        double kAtm = dnsStrike(q.forward, q.atm, q.t, premiumAdjusted_);
        double k25c = strikeForDelta(q.forward, v25c, q.t, 0.25, true, premiumAdjusted_);
        double k25p = strikeForDelta(q.forward, v25p, q.t, -0.25, false, premiumAdjusted_);
        double k10c = tenDelta ? strikeForDelta(q.forward, v10c, q.t, 0.10, true, premiumAdjusted_) : 0;
        double k10p = tenDelta ? strikeForDelta(q.forward, v10p, q.t, -0.10, false, premiumAdjusted_) : 0;
        std::vector<double> ks = tenDelta
                                 ? std::vector<double>{k10p, k25p, kAtm, k25c, k10c}
                                 : std::vector<double>{k25p, kAtm, k25c};
        std::vector<double> vs = tenDelta
                                 ? std::vector<double>{v10p, v25p, q.atm, v25c, v10c}
                                 : std::vector<double>{v25p, q.atm, v25c};
        std::vector<double> x(ks.size());
        for (std::size_t j = 0; j < ks.size(); j++) {
            if (j > 0 && ks[j] <= ks[j - 1]) {
                throw std::runtime_error(
                    "solved strikes not increasing at expiry " + std::to_string(q.t)
                    + " -- check rr/bf signs and magnitudes");
            }
            x[j] = std::log(ks[j] / q.forward);
        }
        lm[i] = std::move(x);
        vv[i] = std::move(vs);
    }
    return FxVolSurface(std::move(ts), std::move(fwds), std::move(lm), std::move(vv),
                        premiumAdjusted_);
}

double FxVolSurface::dnsStrike(double forward, double vol, double tYears, bool premiumAdjusted) {
    double half = 0.5 * vol * vol * tYears;
    return forward * std::exp(premiumAdjusted ? -half : half);
}

double FxVolSurface::strikeForDelta(double forward, double vol, double tYears, double delta,
                                    bool isCall, bool premiumAdjusted) {
    if (isCall ? (delta <= 0 || delta >= 1) : (delta >= 0 || delta <= -1)) {
        throw std::invalid_argument("delta out of range for "
                                    + std::string(isCall ? "call" : "put") + ": "
                                    + std::to_string(delta));
    }
    double sqrtT = std::sqrt(tYears);
    double sv = vol * sqrtT;
    if (!premiumAdjusted) {
        double d1 = isCall ? math_utils::normInv(delta) : -math_utils::normInv(-delta);
        return forward * std::exp(-d1 * sv + 0.5 * sv * sv);
    }
    double lo = forward * std::exp(-8 * sv);
    double hi = forward * std::exp(8 * sv);
    if (isCall) {
        double peak = lo;
        double peakVal = -1;
        int peakIdx = 0;
        for (int i = 0; i <= 200; i++) {
            double k = lo * std::pow(hi / lo, i / 200.0);
            double v = paCallDelta(forward, k, sv);
            if (v > peakVal) {
                peakVal = v;
                peak = k;
                peakIdx = i;
            }
        }
        // The coarse 200-point grid always slightly under-estimates the true
        // continuous maximum. Refine with a golden-section search around the
        // best grid sample so deltas legitimately attainable at the edge of
        // range aren't wrongly rejected as "unattainable".
        {
            double glo = lo * std::pow(hi / lo, std::max(0, peakIdx - 1) / 200.0);
            double ghi = lo * std::pow(hi / lo, std::min(200, peakIdx + 1) / 200.0);
            const double gr = (std::sqrt(5.0) - 1) / 2;
            double a = glo, b = ghi;
            double c = b - gr * (b - a);
            double d = a + gr * (b - a);
            double fc = paCallDelta(forward, c, sv);
            double fd = paCallDelta(forward, d, sv);
            for (int i = 0; i < 60; i++) {
                if (fc > fd) {
                    b = d;
                    d = c;
                    fd = fc;
                    c = b - gr * (b - a);
                    fc = paCallDelta(forward, c, sv);
                } else {
                    a = c;
                    c = d;
                    fc = fd;
                    d = a + gr * (b - a);
                    fd = paCallDelta(forward, d, sv);
                }
            }
            double refinedK = 0.5 * (a + b);
            double refinedVal = paCallDelta(forward, refinedK, sv);
            if (refinedVal > peakVal) {
                peakVal = refinedVal;
                peak = refinedK;
            }
        }
        if (delta > peakVal) {
            throw std::invalid_argument(
                "premium-adjusted call delta " + std::to_string(delta) + " unattainable (max "
                + std::to_string(peakVal) + ") at vol " + std::to_string(vol) + ", t "
                + std::to_string(tYears));
        }
        return bisect([&](double k) { return paCallDelta(forward, k, sv) - delta; }, peak, hi);
    }
    return bisect(
        [&](double k) { return -(k / forward) * math_utils::normCdf(-d2Of(forward, k, sv)) - delta; },
        lo, hi);
}

int FxVolSurface::bracket(double t) const {
    int lo = 0;
    int hi = static_cast<int>(expiries_.size()) - 1;
    while (hi - lo > 1) {
        int mid = (lo + hi) / 2;
        if (expiries_[mid] <= t) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    return lo;
}

double FxVolSurface::smileVol(int i, double x) const {
    const std::vector<double>& xs = logMoneyness_[i];
    const std::vector<double>& vs = vols_[i];
    if (x <= xs[0]) {
        return vs[0];
    }
    int last = static_cast<int>(xs.size()) - 1;
    if (x >= xs[last]) {
        return vs[last];
    }
    int lo = 0;
    while (xs[lo + 1] < x) {
        lo++;
    }
    double w = (x - xs[lo]) / (xs[lo + 1] - xs[lo]);
    return vs[lo] + w * (vs[lo + 1] - vs[lo]);
}

double FxVolSurface::vol(double expiryYears, double strike) const {
    if (strike <= 0) {
        throw std::invalid_argument("strike must be > 0: " + std::to_string(strike));
    }
    int n = static_cast<int>(expiries_.size());
    if (expiryYears <= expiries_[0]) {
        return smileVol(0, std::log(strike / forwards_[0]));
    }
    if (expiryYears >= expiries_[n - 1]) {
        return smileVol(n - 1, std::log(strike / forwards_[n - 1]));
    }
    int lo = bracket(expiryYears);
    int hi = lo + 1;
    double w = (expiryYears - expiries_[lo]) / (expiries_[hi] - expiries_[lo]);
    double forward = std::exp(std::log(forwards_[lo])
                              + w * (std::log(forwards_[hi]) - std::log(forwards_[lo])));
    double x = std::log(strike / forward);
    double vLo = smileVol(lo, x);
    double vHi = smileVol(hi, x);
    double wLo = vLo * vLo * expiries_[lo];
    double wHi = vHi * vHi * expiries_[hi];
    double totalVar = wLo + (wHi - wLo) * w;
    return std::sqrt(totalVar / expiryYears);
}

double FxVolSurface::atmVol(double expiryYears) const {
    int nearest = nearestExpiry(expiryYears);
    double seed = vols_[nearest][vols_[nearest].size() / 2];
    return vol(expiryYears, dnsStrike(forwardAt(expiryYears), seed, expiryYears, premiumAdjusted_));
}

double FxVolSurface::forwardAt(double expiryYears) const {
    int n = static_cast<int>(expiries_.size());
    if (expiryYears <= expiries_[0]) {
        return forwards_[0];
    }
    if (expiryYears >= expiries_[n - 1]) {
        return forwards_[n - 1];
    }
    int lo = bracket(expiryYears);
    double w = (expiryYears - expiries_[lo]) / (expiries_[lo + 1] - expiries_[lo]);
    return std::exp(std::log(forwards_[lo])
                    + w * (std::log(forwards_[lo + 1]) - std::log(forwards_[lo])));
}

FxVolSurface::SmilePillar FxVolSurface::pillar(int i) const {
    std::vector<double> ks(logMoneyness_[i].size());
    for (std::size_t j = 0; j < ks.size(); j++) {
        ks[j] = forwards_[i] * std::exp(logMoneyness_[i][j]);
    }
    return SmilePillar{expiries_[i], forwards_[i], std::move(ks), vols_[i]};
}

int FxVolSurface::nearestExpiry(double t) const {
    int best = 0;
    for (int i = 1; i < static_cast<int>(expiries_.size()); i++) {
        if (std::fabs(expiries_[i] - t) < std::fabs(expiries_[best] - t)) {
            best = i;
        }
    }
    return best;
}

}  // namespace quantfinlib
