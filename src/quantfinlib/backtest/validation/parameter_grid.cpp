#include "quantfinlib/backtest/validation/parameter_grid.hpp"

#include <stdexcept>

namespace quantfinlib {

ParameterGrid& ParameterGrid::add(const std::string& name, std::vector<double> values) {
    if (values.empty()) {
        throw std::invalid_argument("no values for parameter " + name);
    }
    // Java LinkedHashMap.put semantics: re-adding a name replaces its
    // values but keeps the original insertion position.
    for (auto& entry : params_) {
        if (entry.first == name) {
            entry.second = std::move(values);
            return *this;
        }
    }
    params_.emplace_back(name, std::move(values));
    return *this;
}

int ParameterGrid::size() const {
    if (params_.empty()) {
        return 0;
    }
    int n = 1;
    for (const auto& entry : params_) {
        n *= static_cast<int>(entry.second.size());
    }
    return n;
}

std::vector<Params> ParameterGrid::combinations() const {
    if (params_.empty()) {
        return {};
    }
    std::vector<Params> out;
    out.emplace_back();
    for (const auto& entry : params_) {
        std::vector<Params> next;
        next.reserve(out.size() * entry.second.size());
        for (const Params& base : out) {
            for (double v : entry.second) {
                Params combo = base;
                combo[entry.first] = v;
                next.push_back(std::move(combo));
            }
        }
        out = std::move(next);
    }
    return out;
}

}  // namespace quantfinlib
