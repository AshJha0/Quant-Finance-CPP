// Port of com.quantfinlib.microstructure.DayTypeProfiles: day-type-aware
// seasonality container -- one independently-learned profile (typically
// VolumeCurve, VolatilityCurve or SpreadForecaster) per day type. All
// profiles are constructed eagerly up front via the supplied factory.
#pragma once

#include <functional>
#include <stdexcept>
#include <vector>

namespace quantfinlib {

template <typename T>
class DayTypeProfiles {
public:
    /**
     * @param dayTypes number of day types in the caller's taxonomy
     * @param factory  builds one fresh, identically-configured profile per day-type index
     */
    DayTypeProfiles(int dayTypes, std::function<T(int)> factory) {
        if (dayTypes < 1) {
            throw std::invalid_argument("need dayTypes >= 1");
        }
        profiles_.reserve(static_cast<std::size_t>(dayTypes));
        for (int i = 0; i < dayTypes; i++) {
            profiles_.push_back(factory(i));
        }
    }

    /** Variant whose factory does not need the day-type index. */
    DayTypeProfiles(int dayTypes, std::function<T()> factory)
            : DayTypeProfiles(dayTypes, [factory](int) { return factory(); }) {}

    /** The independently-learned profile for dayType. */
    T& profile(int dayType) { return profiles_[static_cast<std::size_t>(dayType)]; }
    const T& profile(int dayType) const { return profiles_[static_cast<std::size_t>(dayType)]; }

    int dayTypes() const { return static_cast<int>(profiles_.size()); }

private:
    std::vector<T> profiles_;
};

}  // namespace quantfinlib
