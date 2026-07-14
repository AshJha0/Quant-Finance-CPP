#include "quantfinlib/pricing/dividend_schedule.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace quantfinlib {

DividendSchedule::DividendSchedule(std::vector<double> exTimes,
                                   std::vector<double> amounts)
    : exTimes_(std::move(exTimes)), amounts_(std::move(amounts)) {}

const DividendSchedule& DividendSchedule::NONE() {
    static const DividendSchedule none{std::vector<double>{}, std::vector<double>{}};
    return none;
}

DividendSchedule DividendSchedule::of(const std::vector<double>& exTimesYears,
                                      const std::vector<double>& amounts) {
    if (exTimesYears.size() != amounts.size()) {
        throw std::invalid_argument("times and amounts must align");
    }
    for (std::size_t i = 0; i < exTimesYears.size(); i++) {
        if (exTimesYears[i] <= 0 || amounts[i] < 0) {
            throw std::invalid_argument("ex-times must be > 0 and amounts >= 0");
        }
        if (i > 0 && exTimesYears[i] <= exTimesYears[i - 1]) {
            throw std::invalid_argument("ex-times must be strictly ascending");
        }
    }
    return DividendSchedule(exTimesYears, amounts);
}

double DividendSchedule::presentValue(double rate, double horizonYears) const {
    double pv = 0;
    for (std::size_t i = 0; i < exTimes_.size() && exTimes_[i] <= horizonYears; i++) {
        pv += amounts_[i] * std::exp(-rate * exTimes_[i]);
    }
    return pv;
}

double DividendSchedule::adjustedSpot(double spot, double rate,
                                      double horizonYears) const {
    double adjusted = spot - presentValue(rate, horizonYears);
    if (adjusted <= 0) {
        throw std::invalid_argument(
                "dividend PV exceeds spot — check amounts/horizon");
    }
    return adjusted;
}

double DividendSchedule::forward(double spot, double rate, double borrow,
                                 double horizonYears) const {
    return adjustedSpot(spot, rate, horizonYears)
            * std::exp((rate - borrow) * horizonYears);
}

double DividendSchedule::europeanPrice(OptionType type, double spot, double strike,
                                       double rate, double borrow, double vol,
                                       double timeYears) const {
    return black_scholes::price(type, adjustedSpot(spot, rate, timeYears), strike,
                                rate, borrow, vol, timeYears);
}

int DividendSchedule::count() const {
    return static_cast<int>(exTimes_.size());
}

}  // namespace quantfinlib
