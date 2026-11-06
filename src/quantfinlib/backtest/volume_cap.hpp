// Volume participation cap shared by the bar engines: no single fill may
// take more than a set share of the bar it trades in.
#pragma once

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/data/bar_series.hpp"

namespace quantfinlib {
namespace volume_cap {

/** A rate must be in [0, 1]; 0 disables the cap. NaN is refused. */
inline void requireRate(double rate) {
    if (!(rate >= 0 && rate <= 1)) {
        throw std::invalid_argument("maxParticipationRate must be in [0, 1]");
    }
}

/** False for a series that carries no volume at all (e.g. spot FX). */
inline bool hasVolumeData(const BarSeries& series) {
    for (int i = 0; i < series.size(); i++) {
        if (series.volume(i) > 0) {
            return true;
        }
    }
    return false;
}

/** Most units tradeable on bar `index`; infinite when no cap applies. */
inline double maxQuantity(const BarSeries& series, int index, double rate, bool volumeKnown) {
    if (rate <= 0 || !volumeKnown) {
        return std::numeric_limits<double>::infinity();
    }
    double volume = series.volume(index);
    return volume > 0 ? rate * volume : 0.0;  // NaN volume does not trade
}

}  // namespace volume_cap
}  // namespace quantfinlib
