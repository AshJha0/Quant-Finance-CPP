// Port of com.quantfinlib.crb.FactorRegistry (Java): dense integer ids for
// risk-factor names — the SymbolRegistry pattern applied to the central
// risk book's factor space, so exposure arithmetic runs over primitive
// arrays while the factor names stay readable (EQ:AAPL, CCY:EUR,
// FXVEGA:EURUSD). Grow-only; ids are registration order.
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace quantfinlib {

class FactorRegistry {
public:
    /** Returns the id for name, registering it on first sight. */
    int id(const std::string& name);

    /** The id if registered, -1 otherwise (never registers). */
    int idIfPresent(const std::string& name) const;

    const std::string& name(int id) const { return names_[id]; }

    int size() const { return static_cast<int>(names_.size()); }

private:
    std::unordered_map<std::string, int> ids_;
    std::vector<std::string> names_;
};

}  // namespace quantfinlib
