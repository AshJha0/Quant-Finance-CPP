#include "quantfinlib/volatility/range_volatility.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {

namespace {

const double FOUR_LN_2 = 4 * std::log(2.0);
const double GK_CLOSE_WEIGHT = 2 * std::log(2.0) - 1;

double rsTerm(double o, double h, double l, double c) {
    return std::log(h / c) * std::log(h / o) + std::log(l / c) * std::log(l / o);
}

void checkBar(double o, double h, double l, double c, int i) {
    // NaN-rejecting: any NaN fails one of these comparisons.
    if (!(l > 0) || !(h >= l) || h == std::numeric_limits<double>::infinity()
            || !(o >= l) || !(o <= h) || !(c >= l) || !(c <= h)) {
        throw std::invalid_argument("bar " + std::to_string(i)
                + " violates H >= O,C >= L > 0: O=" + std::to_string(o)
                + " H=" + std::to_string(h) + " L=" + std::to_string(l)
                + " C=" + std::to_string(c));
    }
}

void checkAligned(std::size_t a, std::size_t b, std::size_t minBars) {
    if (a != b) {
        throw std::invalid_argument("O/H/L/C arrays must be aligned: "
                + std::to_string(a) + " vs " + std::to_string(b));
    }
    if (a < minBars) {
        throw std::invalid_argument("need >= " + std::to_string(minBars)
                + " bars, got " + std::to_string(a));
    }
}

void checkOhlc(const std::vector<double>& open, const std::vector<double>& high,
               const std::vector<double>& low, const std::vector<double>& close,
               std::size_t minBars) {
    checkAligned(open.size(), high.size(), minBars);
    checkAligned(open.size(), low.size(), minBars);
    checkAligned(open.size(), close.size(), minBars);
    for (std::size_t i = 0; i < open.size(); i++) {
        checkBar(open[i], high[i], low[i], close[i], static_cast<int>(i));
    }
}

void checkPeriods(double periodsPerYear) {
    if (!(periodsPerYear > 0) || periodsPerYear == std::numeric_limits<double>::infinity()) {
        throw std::invalid_argument("periodsPerYear must be positive and finite, got "
                                    + std::to_string(periodsPerYear));
    }
}

std::vector<double> opens(const std::vector<Bar>& bars) {
    std::vector<double> v(bars.size());
    for (std::size_t i = 0; i < bars.size(); i++) v[i] = bars[i].open();
    return v;
}
std::vector<double> highs(const std::vector<Bar>& bars) {
    std::vector<double> v(bars.size());
    for (std::size_t i = 0; i < bars.size(); i++) v[i] = bars[i].high();
    return v;
}
std::vector<double> lows(const std::vector<Bar>& bars) {
    std::vector<double> v(bars.size());
    for (std::size_t i = 0; i < bars.size(); i++) v[i] = bars[i].low();
    return v;
}
std::vector<double> closes(const std::vector<Bar>& bars) {
    std::vector<double> v(bars.size());
    for (std::size_t i = 0; i < bars.size(); i++) v[i] = bars[i].close();
    return v;
}

}  // namespace

double RangeVolatility::parkinson(const std::vector<double>& high,
                                  const std::vector<double>& low,
                                  double periodsPerYear) {
    checkAligned(high.size(), low.size(), 1);
    checkPeriods(periodsPerYear);
    double sum = 0;
    for (std::size_t i = 0; i < high.size(); i++) {
        checkBar(high[i], high[i], low[i], high[i], static_cast<int>(i));   // H/L only
        double hl = std::log(high[i] / low[i]);
        sum += hl * hl;
    }
    return std::sqrt(sum / high.size() / FOUR_LN_2 * periodsPerYear);
}

double RangeVolatility::parkinson(const std::vector<Bar>& bars, double periodsPerYear) {
    return parkinson(highs(bars), lows(bars), periodsPerYear);
}

double RangeVolatility::garmanKlass(const std::vector<double>& open,
                                    const std::vector<double>& high,
                                    const std::vector<double>& low,
                                    const std::vector<double>& close,
                                    double periodsPerYear) {
    checkOhlc(open, high, low, close, 1);
    checkPeriods(periodsPerYear);
    double sum = 0;
    for (std::size_t i = 0; i < open.size(); i++) {
        double hl = std::log(high[i] / low[i]);
        double co = std::log(close[i] / open[i]);
        sum += 0.5 * hl * hl - GK_CLOSE_WEIGHT * co * co;
    }
    // The close term can push a single bar negative; the average of a
    // valid sample cannot reasonably be, but floor for safety.
    return std::sqrt(std::max(0.0, sum / open.size()) * periodsPerYear);
}

double RangeVolatility::garmanKlass(const std::vector<Bar>& bars, double periodsPerYear) {
    return garmanKlass(opens(bars), highs(bars), lows(bars), closes(bars), periodsPerYear);
}

double RangeVolatility::rogersSatchell(const std::vector<double>& open,
                                       const std::vector<double>& high,
                                       const std::vector<double>& low,
                                       const std::vector<double>& close,
                                       double periodsPerYear) {
    checkOhlc(open, high, low, close, 1);
    checkPeriods(periodsPerYear);
    double sum = 0;
    for (std::size_t i = 0; i < open.size(); i++) {
        sum += rsTerm(open[i], high[i], low[i], close[i]);
    }
    return std::sqrt(sum / open.size() * periodsPerYear);
}

double RangeVolatility::rogersSatchell(const std::vector<Bar>& bars, double periodsPerYear) {
    return rogersSatchell(opens(bars), highs(bars), lows(bars), closes(bars),
                          periodsPerYear);
}

double RangeVolatility::yangZhang(const std::vector<double>& open,
                                  const std::vector<double>& high,
                                  const std::vector<double>& low,
                                  const std::vector<double>& close,
                                  double periodsPerYear) {
    checkOhlc(open, high, low, close, 3);
    checkPeriods(periodsPerYear);
    int m = static_cast<int>(open.size()) - 1;   // estimation periods
    // Overnight (close-to-open) and open-to-close log returns.
    double meanO = 0;
    double meanC = 0;
    for (int i = 1; i <= m; i++) {
        meanO += std::log(open[i] / close[i - 1]);
        meanC += std::log(close[i] / open[i]);
    }
    meanO /= m;
    meanC /= m;
    double varO = 0;
    double varC = 0;
    double rs = 0;
    for (int i = 1; i <= m; i++) {
        double o = std::log(open[i] / close[i - 1]) - meanO;
        double c = std::log(close[i] / open[i]) - meanC;
        varO += o * o;
        varC += c * c;
        rs += rsTerm(open[i], high[i], low[i], close[i]);
    }
    varO /= m - 1;
    varC /= m - 1;
    rs /= m;
    double k = 0.34 / (1.34 + (m + 1.0) / (m - 1.0));
    return std::sqrt((varO + k * varC + (1 - k) * rs) * periodsPerYear);
}

double RangeVolatility::yangZhang(const std::vector<Bar>& bars, double periodsPerYear) {
    return yangZhang(opens(bars), highs(bars), lows(bars), closes(bars), periodsPerYear);
}

}  // namespace quantfinlib
