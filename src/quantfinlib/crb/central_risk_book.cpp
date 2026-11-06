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

CentralRiskBook::CentralRiskBook(std::string bookCurrency) : bookCurrency_(std::move(bookCurrency)) {
    if (bookCurrency_.size() != 3) {
        throw std::invalid_argument("book currency must be a 3-letter code");
    }
}

void CentralRiskBook::add(const std::string& desk, const std::string& factor, double amount) {
    if (!std::isfinite(amount)) {
        throw std::invalid_argument("non-finite exposure for " + factor);
    }
    addDirect(desk, registry_.id(factor), amount);
}

void CentralRiskBook::put(Flow& flow, const std::string& factor, double amount) {
    add(flow.desk, factor, amount);
    flow.legs.push_back(Leg{registry_.id(factor), amount});
}

void CentralRiskBook::addDirect(const std::string& desk, int id, double amount) {
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
    Flow flow{FlowKind::CASH_EQUITY, desk, symbol};
    flow.units = qty;
    flow.entry = price;
    flow.mark = price;
    put(flow, "EQ:" + symbol, qty * price);
    flows_.push_back(std::move(flow));
    flowsBooked_++;
}

void CentralRiskBook::equityOptionLegs(Flow& f) {
    if (f.time <= 0) {
        return;  // expired: value is intrinsic, no greeks left
    }
    double delta = black_scholes::delta(f.type, f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double gamma = black_scholes::gamma(f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double vega = black_scholes::vega(f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double deltaLeg = f.units * delta * f.mark;
    double gammaLeg = f.units * gamma * f.mark * f.mark / 100;
    double vegaLeg = f.units * vega / 100;
    requireFinite(deltaLeg, "delta leg");
    requireFinite(gammaLeg, "gamma leg");
    requireFinite(vegaLeg, "vega leg");
    put(f, "EQ:" + f.key, deltaLeg);
    put(f, "EQGAMMA:" + f.key, gammaLeg);
    put(f, "EQVEGA:" + f.key, vegaLeg);
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
    Flow flow{FlowKind::EQUITY_OPTION, desk, symbol};
    flow.type = type;
    flow.units = contracts * multiplier;
    flow.mark = spot;
    flow.strike = strike;
    flow.rate = rate;
    flow.carry = carry;
    flow.vol = vol;
    flow.time = timeYears;
    flow.entry = optionValue(flow);
    equityOptionLegs(flow);
    flows_.push_back(std::move(flow));
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
    Flow flow{FlowKind::FX_LINEAR, desk, pair};
    flow.units = baseNotional;
    flow.entry = rate;
    flow.mark = rate;
    put(flow, "CCY:" + pair.substr(0, 3), baseNotional);
    put(flow, "CCY:" + pair.substr(3, 3), quoteLeg);
    flows_.push_back(std::move(flow));
    learnRate(pair, rate);
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
    Flow flow{FlowKind::FX_SWAP, desk, pair};
    flow.units = baseNotional;
    put(flow, "CCY:" + pair.substr(3, 3), quoteImbalance);
    put(flow, "FXPOINTS:" + pair, -baseNotional);
    flows_.push_back(std::move(flow));
    learnRate(pair, nearRate);
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
    Flow flow{FlowKind::FX_LINEAR, desk, pair};
    flow.units = baseNotional;
    flow.entry = fwdRate;
    flow.mark = fwdRate;
    put(flow, "CCY:" + pair.substr(0, 3), baseNotional);
    put(flow, "CCY:" + pair.substr(3, 3), quoteLeg);
    flows_.push_back(std::move(flow));
    pendingFixings_[pair] += std::fabs(baseNotional);
    learnRate(pair, fwdRate);
    flowsBooked_++;
}

void CentralRiskBook::fxOptionLegs(Flow& f) {
    if (f.time <= 0) {
        return;
    }
    double delta = black_scholes::delta(f.type, f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double gamma = black_scholes::gamma(f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double vega = black_scholes::vega(f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
    double deltaBase = f.units * delta;
    double quoteLeg = -deltaBase * f.mark;
    double gammaLeg = f.units * gamma * f.mark * f.mark / 100;
    double vegaLeg = f.units * vega / 100;
    requireFinite(deltaBase, "delta leg");
    requireFinite(quoteLeg, "quote leg");
    requireFinite(gammaLeg, "gamma leg");
    requireFinite(vegaLeg, "vega leg");
    put(f, "CCY:" + f.key.substr(0, 3), deltaBase);
    put(f, "CCY:" + f.key.substr(3, 3), quoteLeg);
    put(f, "FXGAMMA:" + f.key, gammaLeg);
    put(f, "FXVEGA:" + f.key, vegaLeg);
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
    Flow flow{FlowKind::FX_OPTION, desk, pair};
    flow.type = type;
    flow.units = baseNotional;
    flow.mark = spotRate;
    flow.strike = strike;
    flow.rate = domesticRate;
    flow.carry = foreignRate;
    flow.vol = vol;
    flow.time = timeYears;
    flow.entry = optionValue(flow);
    fxOptionLegs(flow);
    flows_.push_back(std::move(flow));
    learnRate(pair, spotRate);
    flowsBooked_++;
}

double CentralRiskBook::optionValue(const Flow& f) {
    if (f.time <= 0) {
        double intrinsic = f.type == OptionType::CALL ? f.mark - f.strike : f.strike - f.mark;
        return std::max(0.0, intrinsic);
    }
    return black_scholes::price(f.type, f.mark, f.strike, f.rate, f.carry, f.vol, f.time);
}

void CentralRiskBook::remark(Flow& f, double mark, double vol, double yearsElapsed) {
    for (const Leg& leg : f.legs) {
        auto id = static_cast<std::size_t>(leg.factor);
        net_[id] -= leg.amount;
        gross_[id] -= std::fabs(leg.amount);
        deskNet_[f.desk][id] -= leg.amount;
    }
    std::vector<Leg> old = std::move(f.legs);
    f.legs.clear();
    f.mark = mark;
    if (!std::isnan(vol)) {
        f.vol = vol;
    }
    f.time -= yearsElapsed;
    switch (f.kind) {
        case FlowKind::CASH_EQUITY:
            put(f, "EQ:" + f.key, f.units * mark);
            break;
        case FlowKind::EQUITY_OPTION:
            equityOptionLegs(f);
            break;
        case FlowKind::FX_OPTION:
            fxOptionLegs(f);
            break;
        case FlowKind::FX_LINEAR:
        case FlowKind::FX_SWAP:
            // Currency legs are contractual amounts: a new rate changes
            // their value, not their size.
            for (const Leg& leg : old) {
                addDirect(f.desk, leg.factor, leg.amount);
            }
            f.legs = std::move(old);
            break;
    }
}

void CentralRiskBook::remarkEquity(const std::string& symbol, double spot, double vol,
                                   double yearsElapsed) {
    requirePositive(spot, "spot");
    if (!std::isnan(vol)) {
        requirePositive(vol, "vol");
    }
    if (!(yearsElapsed >= 0) || !std::isfinite(yearsElapsed)) {
        throw std::invalid_argument("yearsElapsed must be >= 0 and finite");
    }
    for (Flow& f : flows_) {
        if (f.key == symbol && (f.kind == FlowKind::CASH_EQUITY || f.kind == FlowKind::EQUITY_OPTION)) {
            remark(f, spot, vol, yearsElapsed);
        }
    }
}

void CentralRiskBook::remarkFx(const std::string& pair, double rate, double vol,
                               double yearsElapsed) {
    requirePair(pair);
    requirePositive(rate, "rate");
    if (!std::isnan(vol)) {
        requirePositive(vol, "vol");
    }
    if (!(yearsElapsed >= 0) || !std::isfinite(yearsElapsed)) {
        throw std::invalid_argument("yearsElapsed must be >= 0 and finite");
    }
    for (Flow& f : flows_) {
        if (f.key == pair && f.kind != FlowKind::CASH_EQUITY && f.kind != FlowKind::EQUITY_OPTION) {
            remark(f, rate, vol, yearsElapsed);
        }
    }
    learnRate(pair, rate);
}

double CentralRiskBook::inventoryPnl() const {
    double total = 0;
    for (const Flow& f : flows_) {
        switch (f.kind) {
            case FlowKind::CASH_EQUITY:
                total += f.units * (f.mark - f.entry);
                break;
            case FlowKind::EQUITY_OPTION:
                total += f.units * (optionValue(f) - f.entry);
                break;
            case FlowKind::FX_LINEAR:
                total += f.units * (f.mark - f.entry) * toBook(f.key.substr(3, 3));
                break;
            case FlowKind::FX_OPTION:
                total += f.units * (optionValue(f) - f.entry) * toBook(f.key.substr(3, 3));
                break;
            case FlowKind::FX_SWAP:
                break;
        }
    }
    return total;
}

void CentralRiskBook::setFxRate(const std::string& currency, double bookPerUnit) {
    if (currency.size() != 3) {
        throw std::invalid_argument("currency must be a 3-letter code");
    }
    requirePositive(bookPerUnit, "bookPerUnit");
    setRates_[currency] = bookPerUnit;
}

void CentralRiskBook::learnRate(const std::string& pair, double rate) {
    std::string base = pair.substr(0, 3);
    std::string quote = pair.substr(3, 3);
    if (quote == bookCurrency_) {
        learnedRates_[base] = rate;
    } else if (base == bookCurrency_) {
        learnedRates_[quote] = 1 / rate;
    }
}

double CentralRiskBook::toBook(const std::string& currency) const {
    if (currency == bookCurrency_) {
        return 1;
    }
    auto set = setRates_.find(currency);
    if (set != setRates_.end()) {
        return set->second;
    }
    auto learned = learnedRates_.find(currency);
    if (learned != learnedRates_.end()) {
        return learned->second;
    }
    throw std::invalid_argument("no rate from " + currency + " to " + bookCurrency_
                                + ": call setFxRate");
}

double CentralRiskBook::factorToBook(const std::string& factor) const {
    if (factor.rfind("CCY:", 0) == 0) {
        return toBook(factor.substr(4));
    }
    if (factor.rfind("FXPOINTS:", 0) == 0) {
        return toBook(factor.substr(9, 3));  // measured in base currency
    }
    if (factor.rfind("FXGAMMA:", 0) == 0) {
        return toBook(factor.substr(11));  // measured in quote currency
    }
    if (factor.rfind("FXVEGA:", 0) == 0) {
        return toBook(factor.substr(10));
    }
    return 1;  // equity factors are booked in book currency
}

std::vector<double> CentralRiskBook::bookExposures() const {
    std::vector<double> out = netExposures();
    for (std::size_t i = 0; i < out.size(); i++) {
        if (out[i] != 0) {
            out[i] *= factorToBook(registry_.name(static_cast<int>(i)));
        }
    }
    return out;
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
        if (gross_[i] == 0) {
            continue;
        }
        double fx = factorToBook(registry_.name(i));
        sumNet += std::fabs(net_[i]) * fx;
        sumGross += gross_[i] * fx;
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
    std::vector<double> netVec = bookExposures();
    double bookVar = var_engine::deltaNormalVar(netVec, covariance, confidence);
    double bookEs = var_engine::deltaNormalEs(netVec, covariance, confidence);
    double standalone = 0;
    for (const auto& kv : deskNet_) {
        const std::vector<double>& d = kv.second;
        std::vector<double> full(n, 0.0);
        std::size_t copyLen = std::min(d.size(), full.size());
        for (std::size_t i = 0; i < copyLen; i++) {
            full[i] = d[i] == 0 ? 0 : d[i] * factorToBook(registry_.name(static_cast<int>(i)));
        }
        standalone += var_engine::deltaNormalVar(full, covariance, confidence);
    }
    double sumNet = 0;
    double sumGross = 0;
    int booked = std::min(n, static_cast<int>(net_.size()));
    for (int i = 0; i < booked; i++) {
        if (gross_[i] == 0) {
            continue;
        }
        double fx = factorToBook(registry_.name(i));
        sumNet += std::fabs(net_[i]) * fx;
        sumGross += gross_[i] * fx;
    }
    return {sumGross, sumNet, nettingEfficiency(), bookVar, bookEs, standalone,
           standalone - bookVar};
}

}  // namespace quantfinlib
