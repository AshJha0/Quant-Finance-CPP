#include "quantfinlib/crb/factor_registry.hpp"

namespace quantfinlib {

int FactorRegistry::id(const std::string& name) {
    auto it = ids_.find(name);
    if (it != ids_.end()) {
        return it->second;
    }
    int newId = static_cast<int>(names_.size());
    ids_.emplace(name, newId);
    names_.push_back(name);
    return newId;
}

int FactorRegistry::idIfPresent(const std::string& name) const {
    auto it = ids_.find(name);
    return it == ids_.end() ? -1 : it->second;
}

}  // namespace quantfinlib
