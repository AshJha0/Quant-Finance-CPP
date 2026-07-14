#include "quantfinlib/crb/crb_hedge_universe.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

CrbHedgeUniverse::CrbHedgeUniverse(FactorRegistry& registry) : registry_(registry) {}

CrbHedgeUniverse& CrbHedgeUniverse::addSingleFactor(const std::string& name,
                                                    const std::string& factor,
                                                    double costPerUnit) {
    return add(name, costPerUnit, {factor}, {1});
}

CrbHedgeUniverse& CrbHedgeUniverse::addFxForward(const std::string& name, const std::string& pair,
                                                 double rate, double costPerUnit) {
    if (pair.length() != 6) {
        throw std::invalid_argument("pair must be 6 chars like EURUSD: " + pair);
    }
    if (!(rate > 0) || rate == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("rate must be positive and finite");
    }
    return add(name, costPerUnit, {"CCY:" + pair.substr(0, 3), "CCY:" + pair.substr(3, 3)},
              {1, -rate});
}

CrbHedgeUniverse& CrbHedgeUniverse::add(const std::string& name, double costPerUnit,
                                        const std::vector<std::string>& factors,
                                        const std::vector<double>& perUnit) {
    if (name.empty()) {
        throw std::invalid_argument("instrument must be named");
    }
    if (!(costPerUnit >= 0) || costPerUnit == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("costPerUnit must be >= 0 and finite");
    }
    if (factors.size() != perUnit.size() || factors.empty()) {
        throw std::invalid_argument("need aligned, non-empty factor/loading arrays");
    }
    std::vector<int> ids(factors.size());
    for (std::size_t i = 0; i < factors.size(); i++) {
        if (!std::isfinite(perUnit[i])) {
            throw std::invalid_argument("loadings must be finite");
        }
        ids[i] = registry_.id(factors[i]);
    }
    instruments_.push_back({name, costPerUnit, std::move(ids), perUnit});
    return *this;
}

std::vector<std::vector<double>> CrbHedgeUniverse::loadings() const {
    int n = registry_.size();
    int m = static_cast<int>(instruments_.size());
    std::vector<std::vector<double>> out(n, std::vector<double>(m, 0.0));
    for (int i = 0; i < m; i++) {
        const Instrument& inst = instruments_[i];
        for (std::size_t k = 0; k < inst.factorIds.size(); k++) {
            out[inst.factorIds[k]][i] += inst.perUnit[k];
        }
    }
    return out;
}

std::vector<double> CrbHedgeUniverse::costs() const {
    std::vector<double> out(instruments_.size());
    for (std::size_t i = 0; i < out.size(); i++) {
        out[i] = instruments_[i].costPerUnit;
    }
    return out;
}

}  // namespace quantfinlib
