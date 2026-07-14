#include "quantfinlib/marketdata/symbol_registry.hpp"

#include <mutex>
#include <stdexcept>

namespace quantfinlib {

int SymbolRegistry::registerSymbol(const std::string& symbol) {
    std::unique_lock lock(mutex_);
    auto it = ids_.find(symbol);
    if (it != ids_.end()) {
        return it->second;
    }
    int id = static_cast<int>(symbols_.size());
    symbols_.push_back(symbol);
    ids_.emplace(symbol, id);
    return id;
}

int SymbolRegistry::id(const std::string& symbol) const {
    std::shared_lock lock(mutex_);
    auto it = ids_.find(symbol);
    if (it == ids_.end()) {
        throw std::invalid_argument("unregistered symbol: " + symbol);
    }
    return it->second;
}

std::string SymbolRegistry::symbol(int id) const {
    std::shared_lock lock(mutex_);
    return symbols_[static_cast<std::size_t>(id)];
}

int SymbolRegistry::size() const {
    std::shared_lock lock(mutex_);
    return static_cast<int>(symbols_.size());
}

}  // namespace quantfinlib
