#include "quantfinlib/pricing/vol_surface.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>

namespace quantfinlib {

VolSurface::VolSurface(std::map<double, std::map<double, double>> smiles)
    : smiles_(std::move(smiles)) {}

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

VolSurface VolSurface::Builder::build() const {
    if (smiles_.empty()) {
        throw std::runtime_error("no pillar quotes");
    }
    // Value semantics deep-copy the pillars, so the builder stays reusable.
    return VolSurface(smiles_);
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
