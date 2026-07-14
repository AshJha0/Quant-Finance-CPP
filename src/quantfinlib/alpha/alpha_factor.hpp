// Port of com.quantfinlib.alpha.AlphaFactor: a cross-sectional alpha
// factor -- at a bar index, one raw score per symbol where higher = more
// attractive to own. Java's functional interface + default name() becomes
// a small value type wrapping a std::function plus a name, so factors can
// be stored in std::vector<AlphaFactor> (candidate lists, sweeps).
//
// Contract (same as the Java interface):
//  - the returned vector aligns with AlphaContext::symbols();
//  - NaN means "no score" -- every downstream step skips NaN entries;
//  - implementations must only read bars <= index (no look-ahead);
//  - custom factors should honor AlphaContext::isActive the way the
//    built-ins do.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace quantfinlib {

class AlphaContext;

class AlphaFactor {
public:
    using ScoreFn = std::function<std::vector<double>(const AlphaContext&, int)>;

    AlphaFactor(std::string name, ScoreFn fn) : name_(std::move(name)), fn_(std::move(fn)) {}

    std::vector<double> scores(const AlphaContext& ctx, int index) const { return fn_(ctx, index); }

    const std::string& name() const { return name_; }

private:
    std::string name_;
    ScoreFn fn_;
};

}  // namespace quantfinlib
