// Port of com.quantfinlib.alpha.AlphaContext: the research dataset an
// alpha factor operates on -- an index-aligned panel of price series over
// a FROZEN (sorted) symbol order, with optional fundamentals and an
// optional point-in-time universe for survivorship gating.
#pragma once

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/data/fundamentals.hpp"
#include "quantfinlib/data/point_in_time_universe.hpp"

namespace quantfinlib {

class AlphaContext {
public:
    /** Panel without fundamentals (technical factors only). */
    static AlphaContext of(const std::unordered_map<std::string, BarSeries>& data);

    /** Panel with a fundamentals snapshot for value/quality factors. */
    static AlphaContext of(const std::unordered_map<std::string, BarSeries>& data,
                            const std::unordered_map<std::string, Fundamentals>& fundamentals);

    /** The same panel with a point-in-time universe attached. */
    AlphaContext withUniverse(std::shared_ptr<const PointInTimeUniverse> universe) const;

    /** Whether symbol i is in the tradeable cross-section at barIndex. */
    bool isActive(int i, int barIndex) const;

    /** The frozen symbol order every score/weight array aligns with. */
    const std::vector<std::string>& symbols() const { return symbols_; }

    int symbolCount() const { return static_cast<int>(symbols_.size()); }

    /** Panel length in bars (every series has exactly this many). */
    int bars() const { return bars_; }

    /** Price series for symbol index i (the panel axis, not the bar). */
    const BarSeries& series(int i) const { return series_[static_cast<std::size_t>(i)]; }

    /** Fundamentals for symbol index i, or nullptr when unknown. */
    const Fundamentals* fundamentals(int i) const {
        const auto& f = fundamentals_[static_cast<std::size_t>(i)];
        return f.has_value() ? &*f : nullptr;
    }

    /** Bar timestamp at index (taken from the first series). */
    std::int64_t timestamp(int index) const { return series_[0].timestamp(index); }

    /** Simple return of symbol i over (fromIndex, toIndex]. */
    double returnOver(int i, int fromIndex, int toIndex) const;

private:
    AlphaContext(std::vector<std::string> symbols, std::vector<BarSeries> series,
                 std::vector<std::optional<Fundamentals>> fundamentals,
                 std::shared_ptr<const PointInTimeUniverse> universe, int bars);

    std::vector<std::string> symbols_;
    std::vector<BarSeries> series_;
    std::vector<std::optional<Fundamentals>> fundamentals_;
    std::shared_ptr<const PointInTimeUniverse> universe_;
    int bars_;
};

}  // namespace quantfinlib
