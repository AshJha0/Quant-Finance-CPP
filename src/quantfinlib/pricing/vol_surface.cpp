#include "quantfinlib/pricing/vol_surface.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

#include "quantfinlib/pricing/black76.hpp"

namespace quantfinlib {

VolSurface::VolSurface(std::map<double, std::map<double, double>> smiles,
                       std::map<double, double> forwards)
    : smiles_(std::move(smiles)), forwards_(std::move(forwards)) {}

VolSurface::Builder VolSurface::builder() {
    return Builder{};
}

VolSurface::Builder& VolSurface::Builder::add(double expiryYears, double strike,
                                              double vol) {
    // !(x > 0) rejects NaN too: impliedVol returns NaN for unattainable
    // prices, and a NaN pillar poisons every neighboring strike through
    // interpolation.
    if (!(expiryYears > 0) || !(strike > 0) || !(vol > 0)
            || vol == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument(
                "expiry, strike and vol must be positive: " + std::to_string(expiryYears)
                + "/" + std::to_string(strike) + "/" + std::to_string(vol));
    }
    smiles_[expiryYears][strike] = vol;
    return *this;
}

VolSurface::Builder& VolSurface::Builder::addFromPrice(
        OptionType type, double marketPrice, double spot, double strike,
        double rate, double carry, double expiryYears) {
    return add(expiryYears, strike,
               black_scholes::impliedVol(type, marketPrice, spot, strike, rate,
                                         carry, expiryYears));
}

VolSurface::Builder& VolSurface::Builder::forward(double expiryYears, double forwardValue) {
    if (!(expiryYears > 0) || !(forwardValue > 0) || !std::isfinite(forwardValue)) {
        throw std::invalid_argument("expiry and forward must be positive and finite");
    }
    forwards_[expiryYears] = forwardValue;
    return *this;
}

VolSurface VolSurface::Builder::build() const {
    if (smiles_.empty()) {
        throw std::runtime_error("no pillar quotes");
    }
    // Value semantics deep-copy the pillars, so the builder stays reusable.
    if (!forwards_.empty()) {
        bool same = forwards_.size() == smiles_.size();
        for (const auto& [t, smile] : smiles_) {
            (void)smile;
            same = same && forwards_.count(t) == 1;
        }
        if (!same) {
            throw std::invalid_argument("forwards must be given for exactly the quoted expiries");
        }
    }
    return VolSurface(smiles_, forwards_);
}

double VolSurface::vol(double expiryYears, double strike) const {
    // floorEntry / ceilingEntry on the expiry axis.
    auto hi = smiles_.lower_bound(expiryYears);
    auto lo = hi;
    bool hasLo = false;
    if (hi != smiles_.end() && hi->first == expiryYears) {
        lo = hi;
        hasLo = true;
    } else if (lo != smiles_.begin()) {
        --lo;
        hasLo = true;
    }
    if (!hasLo) {
        return smileVol(hi->second, strike);       // before first expiry: flat
    }
    if (hi == smiles_.end()) {
        return smileVol(lo->second, strike);       // beyond last expiry: flat
    }
    if (lo->first == hi->first) {
        return smileVol(lo->second, strike);
    }
    double t1 = lo->first;
    double t2 = hi->first;
    double v1 = smileVol(lo->second, strike);
    double v2 = smileVol(hi->second, strike);
    double w1 = v1 * v1 * t1;
    double w2 = v2 * v2 * t2;
    double w = w1 + (w2 - w1) * (expiryYears - t1) / (t2 - t1);
    return std::sqrt(w / expiryYears);
}

double VolSurface::atmVol(double expiryYears, double forward) const {
    return vol(expiryYears, forward);
}

double VolSurface::price(OptionType type, double spot, double strike,
                         double rate, double carry, double expiryYears) const {
    return black_scholes::price(type, spot, strike, rate, carry,
                                vol(expiryYears, strike), expiryYears);
}

double VolSurface::skew(double expiryYears, double strikeLow,
                        double strikeHigh) const {
    return (vol(expiryYears, strikeHigh) - vol(expiryYears, strikeLow))
            / (strikeHigh - strikeLow);
}

double VolSurface::logMoneyness(double t, double strike) const {
    return forwards_.empty() ? std::log(strike) : std::log(strike / forwards_.at(t));
}

double VolSurface::fromMoneyness(double t, double x) const {
    return forwards_.empty() ? std::exp(x) : forwards_.at(t) * std::exp(x);
}

std::vector<VolSurface::CalendarViolation> VolSurface::calendarViolations(int gridPoints) const {
    if (gridPoints < 2) {
        throw std::invalid_argument("gridPoints must be >= 2");
    }
    std::vector<CalendarViolation> out;
    auto prev = smiles_.end();
    for (auto it = smiles_.begin(); it != smiles_.end(); ++it) {
        if (prev != smiles_.end()) {
            double t1 = prev->first;
            double t2 = it->first;
            double xMin = std::min(logMoneyness(t1, prev->second.begin()->first),
                                   logMoneyness(t2, it->second.begin()->first));
            double xMax = std::max(logMoneyness(t1, prev->second.rbegin()->first),
                                   logMoneyness(t2, it->second.rbegin()->first));
            for (int i = 0; i < gridPoints; i++) {
                double x = xMin + (xMax - xMin) * i / (gridPoints - 1);
                double v1 = smileVol(prev->second, fromMoneyness(t1, x));
                double v2 = smileVol(it->second, fromMoneyness(t2, x));
                double w1 = v1 * v1 * t1;
                double w2 = v2 * v2 * t2;
                if (w2 < w1 * (1 - 1e-12)) {
                    out.push_back({t1, t2, x, w1, w2});
                }
            }
        }
        prev = it;
    }
    return out;
}

double VolSurface::minimumDensity(double expiryYears, double forward, int n) const {
    auto it = smiles_.find(expiryYears);
    if (it == smiles_.end()) {
        throw std::invalid_argument("no pillar expiry " + std::to_string(expiryYears));
    }
    if (!(forward > 0) || n < 2) {
        throw std::invalid_argument("forward must be > 0 and n >= 2");
    }
    const std::map<double, double>& smile = it->second;
    double kMin = smile.begin()->first;
    double kMax = smile.rbegin()->first;
    if (kMax == kMin) {
        kMin *= 0.9;
        kMax *= 1.1;
    }
    double lowest = std::numeric_limits<double>::infinity();
    auto call = [&](double k) {
        return black76::price(OptionType::CALL, forward, k, 0, smileVol(smile, k), expiryYears);
    };
    for (int i = 0; i < n; i++) {
        double k = kMin + (kMax - kMin) * i / (n - 1);
        double h = 1e-4 * k;
        lowest = std::min(lowest, (call(k + h) - 2 * call(k) + call(k - h)) / (h * h));
    }
    return lowest;
}

std::vector<double> VolSurface::expiries() const {
    std::vector<double> out;
    out.reserve(smiles_.size());
    for (const auto& [t, smile] : smiles_) {
        (void)smile;
        out.push_back(t);
    }
    return out;
}

std::vector<double> VolSurface::strikes(double expiryYears) const {
    auto it = smiles_.find(expiryYears);
    if (it == smiles_.end()) {
        throw std::invalid_argument(
                "no pillar expiry " + std::to_string(expiryYears));
    }
    std::vector<double> out;
    out.reserve(it->second.size());
    for (const auto& [k, v] : it->second) {
        (void)v;
        out.push_back(k);
    }
    return out;
}

double VolSurface::smileVol(const std::map<double, double>& smile, double strike) {
    auto hi = smile.lower_bound(strike);
    auto lo = hi;
    bool hasLo = false;
    if (hi != smile.end() && hi->first == strike) {
        return hi->second;                          // exact pillar strike
    }
    if (lo != smile.begin()) {
        --lo;
        hasLo = true;
    }
    if (!hasLo) {
        return hi->second;                          // below the low wing: flat
    }
    if (hi == smile.end()) {
        return lo->second;                          // above the high wing: flat
    }
    double w = (strike - lo->first) / (hi->first - lo->first);
    return lo->second + w * (hi->second - lo->second);
}

}  // namespace quantfinlib
