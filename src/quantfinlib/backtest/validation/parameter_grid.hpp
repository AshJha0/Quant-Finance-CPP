// Port of com.quantfinlib.backtest.validation.ParameterGrid: a named
// parameter grid for strategy optimization; combinations() enumerates
// the cartesian product in deterministic order (parameters expand in
// insertion order, exactly like the Java LinkedHashMap — the first
// parameter added varies slowest). The Java Map<String, Double>
// combination becomes std::map<std::string, double> (Params); factories
// look values up by name, so map ordering is irrelevant to them.
// Gates map to std::invalid_argument.
#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace quantfinlib {

/** One parameter combination: name -> value. */
using Params = std::map<std::string, double>;

class ParameterGrid {
public:
    ParameterGrid& add(const std::string& name, std::vector<double> values);

    int size() const;

    /**
     * All parameter combinations, insertion-ordered and deterministic.
     * Empty for an empty grid, consistent with size() — never a single
     * empty map, which a strategy factory would blow up on.
     */
    std::vector<Params> combinations() const;

private:
    std::vector<std::pair<std::string, std::vector<double>>> params_;
};

}  // namespace quantfinlib
