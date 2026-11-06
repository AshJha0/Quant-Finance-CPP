#include "quantfinlib/alpha/alpha_context.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace quantfinlib {

AlphaContext::AlphaContext(std::vector<std::string> symbols, std::vector<BarSeries> series,
                           std::vector<std::vector<DatedFundamentals>> fundamentals,
                           std::shared_ptr<const PointInTimeUniverse> universe, int bars)
        : symbols_(std::move(symbols)), series_(std::move(series)),
          fundamentals_(std::move(fundamentals)), universe_(std::move(universe)), bars_(bars) {}

AlphaContext AlphaContext::of(const std::unordered_map<std::string, BarSeries>& data) {
    return of(data, std::unordered_map<std::string, std::vector<DatedFundamentals>>{});
}

AlphaContext AlphaContext::ofUndatedSnapshot(
        const std::unordered_map<std::string, BarSeries>& data,
        const std::unordered_map<std::string, Fundamentals>& snapshot) {
    std::unordered_map<std::string, std::vector<DatedFundamentals>> dated;
    for (const auto& kv : snapshot) {
        dated[kv.first].push_back(
                DatedFundamentals{std::numeric_limits<std::int64_t>::min(), kv.second});
    }
    return of(data, dated);
}

AlphaContext AlphaContext::of(
        const std::unordered_map<std::string, BarSeries>& data,
        const std::unordered_map<std::string, std::vector<DatedFundamentals>>& fundamentals) {
    if (data.empty()) {
        throw std::invalid_argument("no series supplied");
    }
    // Sort symbols for a deterministic panel axis regardless of the
    // caller's map implementation -- results must not depend on hash order.
    std::vector<std::string> symbols;
    symbols.reserve(data.size());
    for (const auto& kv : data) {
        symbols.push_back(kv.first);
    }
    std::sort(symbols.begin(), symbols.end());

    int n = data.at(symbols.front()).size();
    std::vector<BarSeries> series;
    series.reserve(symbols.size());
    std::vector<std::vector<DatedFundamentals>> funda(symbols.size());
    for (std::size_t i = 0; i < symbols.size(); i++) {
        const BarSeries& s = data.at(symbols[i]);
        if (s.size() != n) {
            throw std::invalid_argument(
                    "series must be index-aligned (see data.SeriesAligner): " + symbols[i]
                    + " has " + std::to_string(s.size()) + " bars, expected " + std::to_string(n));
        }
        series.push_back(s);
        auto it = fundamentals.find(symbols[i]);
        if (it != fundamentals.end()) {
            funda[i] = it->second;
            std::stable_sort(funda[i].begin(), funda[i].end(),
                             [](const DatedFundamentals& a, const DatedFundamentals& b) {
                                 return a.availableFrom < b.availableFrom;
                             });
        }
    }
    return AlphaContext(std::move(symbols), std::move(series), std::move(funda), nullptr, n);
}

AlphaContext AlphaContext::withUniverse(std::shared_ptr<const PointInTimeUniverse> universe) const {
    return AlphaContext(symbols_, series_, fundamentals_, std::move(universe), bars_);
}

const Fundamentals* AlphaContext::fundamentalsAsOf(int i, int barIndex) const {
    const auto& history = fundamentals_[static_cast<std::size_t>(i)];
    std::int64_t now = timestamp(barIndex);
    auto next = std::upper_bound(history.begin(), history.end(), now,
                                 [](std::int64_t t, const DatedFundamentals& d) {
                                     return t < d.availableFrom;
                                 });
    return next == history.begin() ? nullptr : &std::prev(next)->values;
}

bool AlphaContext::isActive(int i, int barIndex) const {
    return universe_ == nullptr
            || universe_->isMember(symbols_[static_cast<std::size_t>(i)], timestamp(barIndex));
}

double AlphaContext::returnOver(int i, int fromIndex, int toIndex) const {
    const BarSeries& s = series_[static_cast<std::size_t>(i)];
    return s.close(toIndex) / s.close(fromIndex) - 1;
}

}  // namespace quantfinlib
