// Port of com.quantfinlib.alpha.AlphaContext: the research dataset an
// alpha factor operates on -- an index-aligned panel of price series over
// a FROZEN (sorted) symbol order, with optional fundamentals and an
// optional point-in-time universe for survivorship gating.
//
// DEVIATION FROM JAVA: fundamentals are dated. A factor at bar t sees only
// the latest record already published by t, so a figure released in 2024
// cannot rank stocks in 2020. An undated snapshot has to be asked for by
// name (ofUndatedSnapshot): it is visible on every bar, which is look-ahead
// whenever the snapshot is newer than the panel's start.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "quantfinlib/data/bar_series.hpp"
#include "quantfinlib/data/fundamentals.hpp"
#include "quantfinlib/data/point_in_time_universe.hpp"

namespace quantfinlib {

/** A fundamentals record and the time it became public (not its period end). */
struct DatedFundamentals {
    std::int64_t availableFrom = 0;
    Fundamentals values;
};

class AlphaContext {
public:
    /** Panel without fundamentals (technical factors only). */
    static AlphaContext of(const std::unordered_map<std::string, BarSeries>& data);

    /** Panel with as-of dated fundamentals for value/quality factors. */
    static AlphaContext of(
            const std::unordered_map<std::string, BarSeries>& data,
            const std::unordered_map<std::string, std::vector<DatedFundamentals>>& fundamentals);

    /** One undated snapshot visible on every bar. Look-ahead unless it predates the panel. */
    static AlphaContext ofUndatedSnapshot(
            const std::unordered_map<std::string, BarSeries>& data,
            const std::unordered_map<std::string, Fundamentals>& snapshot);

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

    /** Latest fundamentals public at barIndex, or nullptr when none are. */
    const Fundamentals* fundamentalsAsOf(int i, int barIndex) const;

    /**
     * The attached universe, or nullptr. For engines only: it holds terminal
     * events dated in the future, so a factor reading it would look ahead.
     */
    const PointInTimeUniverse* universeForEngine() const { return universe_.get(); }

    /** Bar timestamp at index (taken from the first series). */
    std::int64_t timestamp(int index) const { return series_[0].timestamp(index); }

    /** Simple return of symbol i over (fromIndex, toIndex]. */
    double returnOver(int i, int fromIndex, int toIndex) const;

private:
    AlphaContext(std::vector<std::string> symbols, std::vector<BarSeries> series,
                 std::vector<std::vector<DatedFundamentals>> fundamentals,
                 std::shared_ptr<const PointInTimeUniverse> universe, int bars);

    std::vector<std::string> symbols_;
    std::vector<BarSeries> series_;
    std::vector<std::vector<DatedFundamentals>> fundamentals_;  // sorted by availableFrom
    std::shared_ptr<const PointInTimeUniverse> universe_;
    int bars_;
};

}  // namespace quantfinlib
