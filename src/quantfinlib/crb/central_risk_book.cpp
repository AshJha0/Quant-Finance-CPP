#include "quantfinlib/crb/central_risk_book.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/risk/var_engine.hpp"

namespace quantfinlib {

namespace {

void requireDesk(const std::string& desk) {
    bool blank = desk.empty();
    if (!blank) {
        blank = true;
        for (char c : desk) {
            if (!std::isspace(static_cast<unsigned char>(c))) {
                blank = false;
                break;
            }
        }
    }
    if (blank) {
        throw std::invalid_argument("desk must be named");
    }
}

void requirePair(const std::string& pair) {
    if (pair.length() != 6) {
        throw std::invalid_argument("pair must be 6 chars like EURUSD: " + pair);
    }
}

void requireFinite(double x, const std::string& name) {
    if (!std::isfinite(x)) {
        throw std::invalid_argument(name + " must be finite");
    }
}

void requirePositive(double x, const std::string& name) {
    if (!(x > 0) || x == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument(name + " must be positive and finite");
    }
}

}  // namespace

void CentralRiskBook::add(const std::string& desk, const std::string& factor, double amount) {
    if (!std::isfinite(amount)) {
        throw std::invalid_argument("non-finite exposure for " + factor);
    }
    int id = registry_.id(factor);
    if (id >= static_cast<int>(net_.size())) {
        std::size_t newSize = std::max(net_.size() * 2, static_cast<std::size_t>(id + 1));
        net_.resize(newSize, 0.0);
        gross_.resize(newSize, 0.0);
    }
    net_[id] += amount;
    gross_[id] += std::fabs(amount);
    auto& d = deskNet_[desk];
    if (d.size() < net_.size()) {
        d.resize(net_.size(), 0.0);
    }
    d[id] += amount;
}

void CentralRiskBook::bookCashEquity(const std::string& desk, const std::string& symbol,
                                     double qty, double price) {
    requireDesk(desk);
    requireFinite(qty, "qty");
    requirePositive(price, "price");
    add(desk, "EQ:" + symbol, qty * price);
    flowsBooked_++;
}

void CentralRiskBook::bookEquityOption(const std::string& desk, const std::string& symbol,
                                       OptionType type, double contracts, double multiplier,
                                       double spot, double strike, double rate, double carry,
                                       double vol, double timeYears) {
    requireDesk(desk);
    requireFinite(contracts, "contracts");
    requireFinite(rate, "rate");
    requireFinite(carry, "carry");
    requirePositive(multiplier, "multiplier");
    requirePositive(spot, "spot");
    requirePositive(strike, "strike");
    requirePositive(vol, "vol");
    requirePositive(timeYears, "timeYears");
    double units = contracts * multiplier;
    double delta = black_scholes::delta(type, spot, strike, rate, carry, vol, timeYears);
    double gamma = black_scholes::gamma(spot, strike, rate, carry, vol, timeYears);
    double vega = black_scholes::vega(spot, strike, rate, carry, vol, timeYears);
    double deltaLeg = units * delta * spot;
    double gammaLeg = units * gamma * spot * spot / 100;
    double vegaLeg = units * vega / 100;
    requireFinite(deltaLeg, "delta leg");
    requireFinite(gammaLeg, "gamma leg");
    requireFinite(vegaLeg, "vega leg");
    add(desk, "EQ:" + symbol, deltaLeg);
    add(desk, "EQGAMMA:" + symbol, gammaLeg);
    add(desk, "EQVEGA:" + symbol, vegaLeg);
    flowsBooked_++;
}

void CentralRiskBook::bookFxSpot(const std::string& desk, const std::string& pair,
                                 double baseNotional, double rate) {
    requireDesk(desk);
    requirePair(pair);
    requireFinite(baseNotional, "baseNotional");
    requirePositive(rate, "rate");
    double quoteLeg = -baseNotional * rate;
    requireFinite(quoteLeg, "quote leg");
    add(desk, "CCY:" + pair.substr(0, 3), baseNotional);
    add(desk, "CCY:" + pair.substr(3, 3), quoteLeg);
    flowsBooked_++;
}

void CentralRiskBook::bookFxSwap(const std::string& desk, const std::string& pair,
                                 double baseNotional, double nearRate, double farRate) {
    requireDesk(desk);
    requirePair(pair);
    requireFinite(baseNotional, "baseNotional");
    requirePositive(nearRate, "nearRate");
    requirePositive(farRate, "farRate");
    double quoteImbalance = baseNotional * (farRate - nearRate);
    requireFinite(quoteImbalance, "quote-leg imbalance");
    add(desk, "CCY:" + pair.substr(3, 3), quoteImbalance);
    add(desk, "FXPOINTS:" + pair, -baseNotional);
    flowsBooked_++;
}

void CentralRiskBook::bookNdf(const std::string& desk, const std::string& pair,
                              double baseNotional, double fwdRate) {
    requireDesk(desk);
    requirePair(pair);
    requireFinite(baseNotional, "baseNotional");
    requirePositive(fwdRate, "fwdRate");
    double quoteLeg = -baseNotional * fwdRate;
    requireFinite(quoteLeg, "quote leg");
    add(desk, "CCY:" + pair.substr(0, 3), baseNotional);
    add(desk, "CCY:" + pair.substr(3, 3), quoteLeg);
    pendingFixings_[pair] += std::fabs(baseNotional);
    flowsBooked_++;
}

void CentralRiskBook::bookFxOption(const std::string& desk, const std::string& pair,
                                   OptionType type, double baseNotional, double spotRate,
                                   double strike, double domesticRate, double foreignRate,
                                   double vol, double timeYears) {
    requireDesk(desk);
    requirePair(pair);
    requireFinite(baseNotional, "baseNotional");
    requireFinite(domesticRate, "domesticRate");
    requireFinite(foreignRate, "foreignRate");
    requirePositive(spotRate, "spotRate");
    requirePositive(strike, "strike");
    requirePositive(vol, "vol");
    requirePositive(timeYears, "timeYears");
    double delta = black_scholes::delta(type, spotRate, strike, domesticRate, foreignRate, vol,
                                        timeYears);
    double gamma = black_scholes::gamma(spotRate, strike, domesticRate, foreignRate, vol,
                                        timeYears);
    double vega = black_scholes::vega(spotRate, strike, domesticRate, foreignRate, vol,
                                      timeYears);
    double deltaBase = baseNotional * delta;
    double quoteLeg = -deltaBase * spotRate;
    double gammaLeg = baseNotional * gamma * spotRate * spotRate / 100;
    double vegaLeg = baseNotional * vega / 100;
    requireFinite(deltaBase, "delta leg");
    requireFinite(quoteLeg, "quote leg");
    requireFinite(gammaLeg, "gamma leg");
    requireFinite(vegaLeg, "vega leg");
    add(desk, "CCY:" + pair.substr(0, 3), deltaBase);
    add(desk, "CCY:" + pair.substr(3, 3), quoteLeg);
    add(desk, "FXGAMMA:" + pair, gammaLeg);
    add(desk, "FXVEGA:" + pair, vegaLeg);
    flowsBooked_++;
}

double CentralRiskBook::exposure(const std::string& factor) const {
    int id = registry_.idIfPresent(factor);
    return id < 0 || id >= static_cast<int>(net_.size()) ? 0 : net_[id];
}

double CentralRiskBook::grossExposure(const std::string& factor) const {
    int id = registry_.idIfPresent(factor);
    return id < 0 || id >= static_cast<int>(gross_.size()) ? 0 : gross_[id];
}

double CentralRiskBook::deskExposure(const std::string& desk, const std::string& factor) const {
    auto it = deskNet_.find(desk);
    if (it == deskNet_.end()) {
        return 0;
    }
    int id = registry_.idIfPresent(factor);
    return id < 0 || id >= static_cast<int>(it->second.size()) ? 0 : it->second[id];
}

std::vector<double> CentralRiskBook::netExposures() const {
    std::vector<double> out(registry_.size(), 0.0);
    std::size_t copyLen = std::min(net_.size(), out.size());
    for (std::size_t i = 0; i < copyLen; i++) {
        out[i] = net_[i];
    }
    return out;
}

double CentralRiskBook::nettingEfficiency() const {
    int n = std::min(registry_.size(), static_cast<int>(net_.size()));
    double sumNet = 0;
    double sumGross = 0;
    for (int i = 0; i < n; i++) {
        sumNet += std::fabs(net_[i]);
        sumGross += gross_[i];
    }
    return sumGross <= 0 ? 0 : 1 - sumNet / sumGross;
}

double CentralRiskBook::pendingFixing(const std::string& pair) const {
    auto it = pendingFixings_.find(pair);
    return it == pendingFixings_.end() ? 0.0 : it->second;
}

void CentralRiskBook::settleFixing(const std::string& pair, double notional) {
    requirePair(pair);
    requirePositive(notional, "notional");
    auto it = pendingFixings_.find(pair);
    double pending = it == pendingFixings_.end() ? 0.0 : it->second;
    if (notional > pending + 1e-9) {
        throw std::invalid_argument("settling " + std::to_string(notional) + " but only "
                                    + std::to_string(pending) + " is pending on " + pair);
    }
    double remaining = pending - notional;
    if (remaining <= 1e-9) {
        pendingFixings_.erase(pair);
    } else {
        pendingFixings_[pair] = remaining;
    }
}

std::set<std::string> CentralRiskBook::desks() const {
    std::set<std::string> out;
    for (const auto& kv : deskNet_) {
        out.insert(kv.first);
    }
    return out;
}

CentralRiskBook::CrbReport CentralRiskBook::report(
    const std::vector<std::vector<double>>& covariance, double confidence) const {
    int n = registry_.size();
    if (static_cast<int>(covariance.size()) != n) {
        throw std::invalid_argument("covariance must be n x n over factors().name(i) in registry order");
    }
    std::vector<double> netVec = netExposures();
    double bookVar = var_engine::deltaNormalVar(netVec, covariance, confidence);
    double bookEs = var_engine::deltaNormalEs(netVec, covariance, confidence);
    double standalone = 0;
    for (const auto& kv : deskNet_) {
        const std::vector<double>& d = kv.second;
        std::vector<double> full(n, 0.0);
        std::size_t copyLen = std::min(d.size(), full.size());
        for (std::size_t i = 0; i < copyLen; i++) {
            full[i] = d[i];
        }
        standalone += var_engine::deltaNormalVar(full, covariance, confidence);
    }
    double sumNet = 0;
    double sumGross = 0;
    int booked = std::min(n, static_cast<int>(net_.size()));
    for (int i = 0; i < booked; i++) {
        sumNet += std::fabs(net_[i]);
        sumGross += gross_[i];
    }
    return {sumGross, sumNet, nettingEfficiency(), bookVar, bookEs, standalone,
           standalone - bookVar};
}

}  // namespace quantfinlib
