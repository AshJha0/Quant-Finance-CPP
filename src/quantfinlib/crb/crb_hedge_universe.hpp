// Port of com.quantfinlib.crb.CrbHedgeUniverse (Java): the hedge-instrument
// universe, aligned to a book's factor registry. Each instrument declares
// what ONE UNIT of its notional does to the factor space, in the same
// conventions CentralRiskBook books with: addSingleFactor (1-for-1 one
// factor), addFxForward (one unit of base notional loads CCY:<base> +1 and
// CCY:<quote> -rate), add (general per-unit loadings). Factors named here
// are REGISTERED on the shared registry if new. Research lane,
// single-threaded.
#pragma once

#include <string>
#include <vector>

#include "quantfinlib/crb/factor_registry.hpp"

namespace quantfinlib {

class CrbHedgeUniverse {
public:
    /** @param registry the book's registry — book.factors() */
    explicit CrbHedgeUniverse(FactorRegistry& registry);

    /** An instrument that is one unit of exactly one factor. */
    CrbHedgeUniverse& addSingleFactor(const std::string& name, const std::string& factor,
                                      double costPerUnit);

    /**
     * An FX forward/spot hedge on pair: one unit of base notional loads
     * the two currency legs exactly as a booked trade would.
     */
    CrbHedgeUniverse& addFxForward(const std::string& name, const std::string& pair,
                                   double rate, double costPerUnit);

    /** A general instrument: per-unit loadings onto named factors. */
    CrbHedgeUniverse& add(const std::string& name, double costPerUnit,
                          const std::vector<std::string>& factors,
                          const std::vector<double>& perUnit);

    /**
     * The loadings matrix [factor][instrument] at the registry's CURRENT
     * size — call after all booking/adding, alongside costs().
     */
    std::vector<std::vector<double>> loadings() const;

    /** Per-unit costs aligned with loadings() columns. */
    std::vector<double> costs() const;

    /** Instrument name for a CrbAutoHedger::HedgeOrder instrument index. */
    const std::string& name(int instrument) const { return instruments_[instrument].name; }

    int size() const { return static_cast<int>(instruments_.size()); }

private:
    struct Instrument {
        std::string name;
        double costPerUnit;
        std::vector<int> factorIds;
        std::vector<double> perUnit;
    };

    FactorRegistry& registry_;
    std::vector<Instrument> instruments_;
};

}  // namespace quantfinlib
