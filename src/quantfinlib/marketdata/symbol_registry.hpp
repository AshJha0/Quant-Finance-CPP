// Port of com.quantfinlib.marketdata.SymbolRegistry (Java): interns
// instrument symbols to dense int ids so the hot path never touches
// std::string hashing or map lookups: resolve the id once at subscription
// time, then publish and dispatch with primitive ints only.
//
// Threading: Java guards the append-only symbol array with `synchronized
// register` plus a volatile array (readers never block); the C++ port uses
// a std::shared_mutex instead -- registerSymbol() takes the exclusive lock
// (cold path, setup only), id()/symbol()/size() take the shared lock. This
// is a deliberate simplification (not lock-free on the read side) because,
// exactly as in Java, every call here is setup/subscription work, never the
// per-tick hot path.
//
// Naming note: Java's `register(String)` is renamed `registerSymbol` here
// because `register` is a reserved (if deprecated) C++ keyword.
#pragma once

#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace quantfinlib {

class SymbolRegistry {
public:
    /** Registers (or looks up) a symbol; returns its stable dense id. */
    int registerSymbol(const std::string& symbol);

    /** Id of an already-registered symbol. @throws std::invalid_argument if unknown. */
    int id(const std::string& symbol) const;

    std::string symbol(int id) const;

    int size() const;

private:
    mutable std::shared_mutex mutex_;
    std::unordered_map<std::string, int> ids_;
    std::vector<std::string> symbols_;
};

}  // namespace quantfinlib
