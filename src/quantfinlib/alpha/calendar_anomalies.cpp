#include "quantfinlib/alpha/calendar_anomalies.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "quantfinlib/rates/local_date.hpp"
#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace calendar_anomalies {

namespace {

constexpr std::int64_t kMillisPerDay = 86'400'000LL;

/** Floor-divide toward negative infinity, like Instant.ofEpochMilli(...).atZone(UTC). */
LocalDate toDate(std::int64_t epochMillis) {
    std::int64_t day = epochMillis >= 0 ? epochMillis / kMillisPerDay
                                        : -((-epochMillis + kMillisPerDay - 1) / kMillisPerDay);
    return LocalDate::ofEpochDay(day);
}

double square(double x) { return x * x; }

void requireAligned(const std::vector<double>& returns, const std::vector<std::int64_t>& epochMillis) {
    if (returns.size() != epochMillis.size() || returns.size() < 30) {
        throw std::invalid_argument("need >= 30 aligned observations");
    }
    for (double r : returns) {
        if (!std::isfinite(r)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
}

}  // namespace

DayOfWeekProfile dayOfWeek(const std::vector<double>& returns, const std::vector<std::int64_t>& epochMillis) {
    requireAligned(returns, epochMillis);
    std::vector<std::vector<double>> byDay(7, std::vector<double>(returns.size()));
    std::vector<int> counts(7, 0);
    for (std::size_t i = 0; i < returns.size(); i++) {
        int d = toDate(epochMillis[i]).dayOfWeek() - 1;
        byDay[static_cast<std::size_t>(d)][static_cast<std::size_t>(counts[static_cast<std::size_t>(d)]++)] =
                returns[i];
    }
    DayOfWeekProfile profile;
    profile.meanReturn.assign(7, 0.0);
    profile.tStat.assign(7, 0.0);
    profile.count = counts;
    for (int d = 0; d < 7; d++) {
        auto idx = static_cast<std::size_t>(d);
        if (counts[idx] < 2) {
            profile.meanReturn[idx] =
                    counts[idx] == 1 ? byDay[idx][0] : std::numeric_limits<double>::quiet_NaN();
            profile.tStat[idx] = std::numeric_limits<double>::quiet_NaN();
            continue;
        }
        double m = math_utils::mean(byDay[idx], 0, counts[idx]);
        double se = math_utils::stdDevSample(byDay[idx], 0, counts[idx]) / std::sqrt(static_cast<double>(counts[idx]));
        profile.meanReturn[idx] = m;
        profile.tStat[idx] =
                se > 0 ? m / se : (m > 0 ? std::numeric_limits<double>::infinity()
                                          : (m < 0 ? -std::numeric_limits<double>::infinity() : 0.0));
    }
    return profile;
}

TurnOfMonth turnOfMonth(const std::vector<double>& returns, const std::vector<std::int64_t>& epochMillis,
                        int daysBeforeMonthEnd, int daysAfterMonthStart) {
    requireAligned(returns, epochMillis);
    if (daysBeforeMonthEnd < 0 || daysAfterMonthStart < 0
        || daysBeforeMonthEnd + daysAfterMonthStart == 0) {
        throw std::invalid_argument("the window must cover at least one day");
    }
    std::vector<double> inside(returns.size());
    std::vector<double> outside(returns.size());
    int nIn = 0;
    int nOut = 0;
    for (std::size_t i = 0; i < returns.size(); i++) {
        LocalDate d = toDate(epochMillis[i]);
        int lengthOfMonth = LocalDate::lengthOfMonth(d.year(), d.month());
        bool in = d.day() <= daysAfterMonthStart || d.day() > lengthOfMonth - daysBeforeMonthEnd;
        if (in) {
            inside[static_cast<std::size_t>(nIn++)] = returns[i];
        } else {
            outside[static_cast<std::size_t>(nOut++)] = returns[i];
        }
    }
    if (nIn < 2 || nOut < 2) {
        throw std::invalid_argument("both windows need >= 2 observations");
    }
    double mIn = math_utils::mean(inside, 0, nIn);
    double mOut = math_utils::mean(outside, 0, nOut);
    double vIn = square(math_utils::stdDevSample(inside, 0, nIn));
    double vOut = square(math_utils::stdDevSample(outside, 0, nOut));
    double se = std::sqrt(vIn / nIn + vOut / nOut);
    double diff = mIn - mOut;
    double t = se > 0 ? diff / se
                      : (diff > 0 ? std::numeric_limits<double>::infinity()
                                  : (diff < 0 ? -std::numeric_limits<double>::infinity() : 0.0));
    TurnOfMonth result;
    result.insideMean = mIn;
    result.outsideMean = mOut;
    result.tStat = t;
    result.insideCount = nIn;
    result.outsideCount = nOut;
    return result;
}

}  // namespace calendar_anomalies
}  // namespace quantfinlib
