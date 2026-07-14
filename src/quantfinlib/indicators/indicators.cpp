#include "quantfinlib/indicators/indicators.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

constexpr double NaN = std::numeric_limits<double>::quiet_NaN();

void checkPeriod(int period) {
    if (period < 1) {
        throw std::invalid_argument("period must be >= 1: " + std::to_string(period));
    }
}

double toRsi(double avgGain, double avgLoss) {
    if (avgLoss == 0) {
        return avgGain == 0 ? 50 : 100;
    }
    return 100 - 100 / (1 + avgGain / avgLoss);
}

std::vector<double> smoothIgnoringNan(const std::vector<double>& v, int period) {
    if (period <= 1) {
        return v;
    }
    std::vector<double> out = math_utils::nanArray(static_cast<int>(v.size()));
    for (int i = 0; i < static_cast<int>(v.size()); i++) {
        double s = 0;
        int cnt = 0;
        for (int j = std::max(0, i - period + 1); j <= i; j++) {
            if (!std::isnan(v[j])) {
                s += v[j];
                cnt++;
            }
        }
        if (cnt == period) {
            out[i] = s / period;
        }
    }
    return out;
}

std::vector<double> extractCloses(const std::vector<Bar>& s) {
    std::vector<double> v(s.size());
    for (std::size_t i = 0; i < s.size(); i++) v[i] = s[i].close();
    return v;
}

std::vector<double> extractHighs(const std::vector<Bar>& s) {
    std::vector<double> v(s.size());
    for (std::size_t i = 0; i < s.size(); i++) v[i] = s[i].high();
    return v;
}

std::vector<double> extractLows(const std::vector<Bar>& s) {
    std::vector<double> v(s.size());
    for (std::size_t i = 0; i < s.size(); i++) v[i] = s[i].low();
    return v;
}

std::vector<double> midChannel(const std::vector<Bar>& s, int period) {
    std::vector<double> hh = Indicators::highest(extractHighs(s), period);
    std::vector<double> ll = Indicators::lowest(extractLows(s), period);
    std::vector<double> out = math_utils::nanArray(static_cast<int>(s.size()));
    for (std::size_t i = 0; i < out.size(); i++) {
        if (!std::isnan(hh[i])) {
            out[i] = (hh[i] + ll[i]) / 2;
        }
    }
    return out;
}

}  // namespace

// ------------------------------------------------------------------
// Moving averages
// ------------------------------------------------------------------

std::vector<double> Indicators::sma(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    if (n < period) {
        return out;
    }
    double sum = 0;
    for (int i = 0; i < n; i++) {
        sum += v[i];
        if (i >= period) {
            sum -= v[i - period];
        }
        if (i >= period - 1) {
            out[i] = sum / period;
        }
    }
    return out;
}

std::vector<double> Indicators::ema(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    if (n < period) {
        return out;
    }
    double sum = 0;
    for (int i = 0; i < period; i++) {
        sum += v[i];
    }
    double prev = sum / period;
    out[period - 1] = prev;
    double k = 2.0 / (period + 1);
    for (int i = period; i < n; i++) {
        prev += (v[i] - prev) * k;
        out[i] = prev;
    }
    return out;
}

std::vector<double> Indicators::wma(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    double denom = period * (period + 1) / 2.0;
    for (int i = period - 1; i < n; i++) {
        double s = 0;
        for (int j = 0; j < period; j++) {
            s += v[i - j] * (period - j);
        }
        out[i] = s / denom;
    }
    return out;
}

// ------------------------------------------------------------------
// Momentum / oscillators
// ------------------------------------------------------------------

std::vector<double> Indicators::rsi(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    if (n <= period) {
        return out;
    }
    double gain = 0, loss = 0;
    for (int i = 1; i <= period; i++) {
        double d = v[i] - v[i - 1];
        if (d > 0) {
            gain += d;
        } else {
            loss -= d;
        }
    }
    double avgGain = gain / period, avgLoss = loss / period;
    out[period] = toRsi(avgGain, avgLoss);
    for (int i = period + 1; i < n; i++) {
        double d = v[i] - v[i - 1];
        avgGain = (avgGain * (period - 1) + std::max(d, 0.0)) / period;
        avgLoss = (avgLoss * (period - 1) + std::max(-d, 0.0)) / period;
        out[i] = toRsi(avgGain, avgLoss);
    }
    return out;
}

std::vector<double> Indicators::roc(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period; i < n; i++) {
        out[i] = (v[i] / v[i - period] - 1) * 100;
    }
    return out;
}

std::vector<double> Indicators::momentum(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period; i < n; i++) {
        out[i] = v[i] - v[i - period];
    }
    return out;
}

Indicators::Macd Indicators::macd(const std::vector<double>& close, int fastPeriod,
                                  int slowPeriod, int signalPeriod) {
    std::vector<double> fast = ema(close, fastPeriod);
    std::vector<double> slow = ema(close, slowPeriod);
    int n = static_cast<int>(close.size());
    std::vector<double> line = math_utils::nanArray(n);
    for (int i = 0; i < n; i++) {
        line[i] = fast[i] - slow[i];
    }
    std::vector<double> signal = math_utils::nanArray(n);
    std::vector<double> hist = math_utils::nanArray(n);
    int start = slowPeriod - 1;
    if (start < n) {
        // The signal EMA is seeded on the VALID slice of the line — no
        // pre-history bias from the NaN warm-up region.
        std::vector<double> valid(line.begin() + start, line.end());
        std::vector<double> sigValid = ema(valid, signalPeriod);
        for (int i = 0; i < static_cast<int>(sigValid.size()); i++) {
            signal[start + i] = sigValid[i];
            hist[start + i] = line[start + i] - sigValid[i];
        }
    }
    return Macd{std::move(line), std::move(signal), std::move(hist)};
}

Indicators::StochRsi Indicators::stochasticRsi(const std::vector<double>& close,
                                               int rsiPeriod, int stochPeriod,
                                               int kSmooth, int dSmooth) {
    std::vector<double> r = rsi(close, rsiPeriod);
    int n = static_cast<int>(close.size());
    std::vector<double> raw = math_utils::nanArray(n);
    for (int i = rsiPeriod + stochPeriod - 1; i < n; i++) {
        double hi = -std::numeric_limits<double>::infinity();
        double lo = std::numeric_limits<double>::infinity();
        for (int j = i - stochPeriod + 1; j <= i; j++) {
            if (!std::isnan(r[j])) {
                hi = std::max(hi, r[j]);
                lo = std::min(lo, r[j]);
            }
        }
        raw[i] = hi > lo ? (r[i] - lo) / (hi - lo) * 100 : 50;
    }
    std::vector<double> k = smoothIgnoringNan(raw, kSmooth);
    std::vector<double> d = smoothIgnoringNan(k, dSmooth);
    return StochRsi{std::move(k), std::move(d)};
}

std::vector<double> Indicators::williamsR(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    int n = static_cast<int>(s.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        double hh = -std::numeric_limits<double>::infinity();
        double ll = std::numeric_limits<double>::infinity();
        for (int j = i - period + 1; j <= i; j++) {
            hh = std::max(hh, s[j].high());
            ll = std::min(ll, s[j].low());
        }
        out[i] = hh > ll ? -100 * (hh - s[i].close()) / (hh - ll) : -50;
    }
    return out;
}

std::vector<double> Indicators::cci(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    int n = static_cast<int>(s.size());
    std::vector<double> tp(n);
    for (int i = 0; i < n; i++) {
        tp[i] = (s[i].high() + s[i].low() + s[i].close()) / 3.0;
    }
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        double m = math_utils::mean(tp, i - period + 1, i + 1);
        double dev = 0;
        for (int j = i - period + 1; j <= i; j++) {
            dev += std::abs(tp[j] - m);
        }
        dev /= period;
        out[i] = dev == 0 ? 0 : (tp[i] - m) / (0.015 * dev);
    }
    return out;
}

// ------------------------------------------------------------------
// Volatility / range
// ------------------------------------------------------------------

std::vector<double> Indicators::trueRange(const std::vector<Bar>& s) {
    int n = static_cast<int>(s.size());
    std::vector<double> tr(n);
    tr[0] = s[0].high() - s[0].low();
    for (int i = 1; i < n; i++) {
        double hl = s[i].high() - s[i].low();
        double hc = std::abs(s[i].high() - s[i - 1].close());
        double lc = std::abs(s[i].low() - s[i - 1].close());
        tr[i] = std::max(hl, std::max(hc, lc));
    }
    return tr;
}

std::vector<double> Indicators::atr(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    std::vector<double> tr = trueRange(s);
    int n = static_cast<int>(tr.size());
    std::vector<double> out = math_utils::nanArray(n);
    if (n < period) {
        return out;
    }
    double sum = 0;
    for (int i = 0; i < period; i++) {
        sum += tr[i];
    }
    double prev = sum / period;
    out[period - 1] = prev;
    for (int i = period; i < n; i++) {
        prev = (prev * (period - 1) + tr[i]) / period;
        out[i] = prev;
    }
    return out;
}

Indicators::Adx Indicators::adx(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    int n = static_cast<int>(s.size());
    std::vector<double> adxOut = math_utils::nanArray(n);
    std::vector<double> plusDi = math_utils::nanArray(n);
    std::vector<double> minusDi = math_utils::nanArray(n);
    if (n <= 2 * period) {
        return Adx{std::move(adxOut), std::move(plusDi), std::move(minusDi)};
    }
    std::vector<double> tr = trueRange(s);
    double smTr = 0, smPlus = 0, smMinus = 0;
    for (int i = 1; i <= period; i++) {
        smTr += tr[i];
        double up = s[i].high() - s[i - 1].high();
        double dn = s[i - 1].low() - s[i].low();
        smPlus += (up > dn && up > 0) ? up : 0;
        smMinus += (dn > up && dn > 0) ? dn : 0;
    }
    std::vector<double> dx = math_utils::nanArray(n);
    for (int i = period; i < n; i++) {
        if (i > period) {
            double up = s[i].high() - s[i - 1].high();
            double dn = s[i - 1].low() - s[i].low();
            smTr = smTr - smTr / period + tr[i];
            smPlus = smPlus - smPlus / period + ((up > dn && up > 0) ? up : 0);
            smMinus = smMinus - smMinus / period + ((dn > up && dn > 0) ? dn : 0);
        }
        double pdi = smTr == 0 ? 0 : 100 * smPlus / smTr;
        double mdi = smTr == 0 ? 0 : 100 * smMinus / smTr;
        plusDi[i] = pdi;
        minusDi[i] = mdi;
        double sum = pdi + mdi;
        dx[i] = sum == 0 ? 0 : 100 * std::abs(pdi - mdi) / sum;
    }
    // ADX = Wilder average of DX
    double acc = 0;
    for (int i = period; i < 2 * period; i++) {
        acc += dx[i];
    }
    double prev = acc / period;
    adxOut[2 * period - 1] = prev;
    for (int i = 2 * period; i < n; i++) {
        prev = (prev * (period - 1) + dx[i]) / period;
        adxOut[i] = prev;
    }
    return Adx{std::move(adxOut), std::move(plusDi), std::move(minusDi)};
}

Indicators::Bollinger Indicators::bollinger(const std::vector<double>& close,
                                            int period, double k) {
    std::vector<double> mid = sma(close, period);
    int n = static_cast<int>(close.size());
    std::vector<double> up = math_utils::nanArray(n);
    std::vector<double> lo = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        double sd = math_utils::stdDevP(close, i - period + 1, i + 1);
        up[i] = mid[i] + k * sd;
        lo[i] = mid[i] - k * sd;
    }
    return Bollinger{std::move(up), std::move(mid), std::move(lo)};
}

Indicators::Keltner Indicators::keltner(const std::vector<Bar>& s, int emaPeriod,
                                        int atrPeriod, double multiplier) {
    std::vector<double> mid = ema(extractCloses(s), emaPeriod);
    std::vector<double> a = atr(s, atrPeriod);
    int n = static_cast<int>(s.size());
    std::vector<double> up = math_utils::nanArray(n);
    std::vector<double> lo = math_utils::nanArray(n);
    for (int i = 0; i < n; i++) {
        if (!std::isnan(mid[i]) && !std::isnan(a[i])) {
            up[i] = mid[i] + multiplier * a[i];
            lo[i] = mid[i] - multiplier * a[i];
        }
    }
    return Keltner{std::move(up), std::move(mid), std::move(lo)};
}

Indicators::Donchian Indicators::donchian(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    int n = static_cast<int>(s.size());
    std::vector<double> up = highest(extractHighs(s), period);
    std::vector<double> lo = lowest(extractLows(s), period);
    std::vector<double> mid = math_utils::nanArray(n);
    for (int i = 0; i < n; i++) {
        if (!std::isnan(up[i])) {
            mid[i] = (up[i] + lo[i]) / 2;
        }
    }
    return Donchian{std::move(up), std::move(mid), std::move(lo)};
}

// ------------------------------------------------------------------
// Volume
// ------------------------------------------------------------------

std::vector<double> Indicators::obv(const std::vector<Bar>& s) {
    int n = static_cast<int>(s.size());
    std::vector<double> out(n, 0.0);
    for (int i = 1; i < n; i++) {
        double d = s[i].close() - s[i - 1].close();
        out[i] = out[i - 1] + (d > 0 ? s[i].volume() : d < 0 ? -s[i].volume() : 0);
    }
    return out;
}

std::vector<double> Indicators::vwap(const std::vector<Bar>& s) {
    int n = static_cast<int>(s.size());
    std::vector<double> out(n);
    double pv = 0, vol = 0;
    for (int i = 0; i < n; i++) {
        double tp = (s[i].high() + s[i].low() + s[i].close()) / 3.0;
        pv += tp * s[i].volume();
        vol += s[i].volume();
        out[i] = vol == 0 ? tp : pv / vol;
    }
    return out;
}

std::vector<double> Indicators::cmf(const std::vector<Bar>& s, int period) {
    checkPeriod(period);
    int n = static_cast<int>(s.size());
    std::vector<double> mfv(n);
    for (int i = 0; i < n; i++) {
        double range = s[i].high() - s[i].low();
        double mult = range == 0
                ? 0
                : ((s[i].close() - s[i].low()) - (s[i].high() - s[i].close())) / range;
        mfv[i] = mult * s[i].volume();
    }
    std::vector<double> out = math_utils::nanArray(n);
    double sumMfv = 0, sumVol = 0;
    for (int i = 0; i < n; i++) {
        sumMfv += mfv[i];
        sumVol += s[i].volume();
        if (i >= period) {
            sumMfv -= mfv[i - period];
            sumVol -= s[i - period].volume();
        }
        if (i >= period - 1) {
            out[i] = sumVol == 0 ? 0 : sumMfv / sumVol;
        }
    }
    return out;
}

// ------------------------------------------------------------------
// Trend systems
// ------------------------------------------------------------------

Indicators::SuperTrend Indicators::superTrend(const std::vector<Bar>& s, int period,
                                              double multiplier) {
    int n = static_cast<int>(s.size());
    std::vector<double> a = atr(s, period);
    std::vector<double> value = math_utils::nanArray(n);
    std::vector<int> dir(n, 0);
    double fUp = NaN, fLo = NaN;
    int trend = 1;
    for (int i = period - 1; i < n; i++) {
        double mid = (s[i].high() + s[i].low()) / 2;
        double bUp = mid + multiplier * a[i];
        double bLo = mid - multiplier * a[i];
        if (std::isnan(fUp)) {
            fUp = bUp;
            fLo = bLo;
        } else {
            fUp = (bUp < fUp || s[i - 1].close() > fUp) ? bUp : fUp;
            fLo = (bLo > fLo || s[i - 1].close() < fLo) ? bLo : fLo;
        }
        if (trend == 1 && s[i].close() < fLo) {
            trend = -1;
        } else if (trend == -1 && s[i].close() > fUp) {
            trend = 1;
        }
        dir[i] = trend;
        value[i] = trend == 1 ? fLo : fUp;
    }
    return SuperTrend{std::move(value), std::move(dir)};
}

Indicators::Ichimoku Indicators::ichimoku(const std::vector<Bar>& s, int tenkanPeriod,
                                          int kijunPeriod, int senkouBPeriod) {
    int n = static_cast<int>(s.size());
    std::vector<double> tenkan = midChannel(s, tenkanPeriod);
    std::vector<double> kijun = midChannel(s, kijunPeriod);
    std::vector<double> senkouA = math_utils::nanArray(n);
    std::vector<double> senkouB = math_utils::nanArray(n);
    std::vector<double> chikou = math_utils::nanArray(n);
    std::vector<double> spanBBase = midChannel(s, senkouBPeriod);
    for (int i = 0; i < n; i++) {
        int fwd = i + kijunPeriod;
        if (fwd < n) {
            if (!std::isnan(tenkan[i]) && !std::isnan(kijun[i])) {
                senkouA[fwd] = (tenkan[i] + kijun[i]) / 2;
            }
            senkouB[fwd] = spanBBase[i];
        }
        int back = i - kijunPeriod;
        if (back >= 0) {
            chikou[back] = s[i].close();
        }
    }
    return Ichimoku{std::move(tenkan), std::move(kijun), std::move(senkouA),
                    std::move(senkouB), std::move(chikou)};
}

std::vector<double> Indicators::parabolicSar(const std::vector<Bar>& s, double afStart,
                                             double afStep, double afMax) {
    int n = static_cast<int>(s.size());
    std::vector<double> out = math_utils::nanArray(n);
    if (n < 2) {
        return out;
    }
    bool up = s[1].close() >= s[0].close();
    double sar = up ? std::min(s[0].low(), s[1].low()) : std::max(s[0].high(), s[1].high());
    double ep = up ? std::max(s[0].high(), s[1].high()) : std::min(s[0].low(), s[1].low());
    double af = afStart;
    out[1] = sar;
    for (int i = 2; i < n; i++) {
        sar += af * (ep - sar);
        if (up) {
            sar = std::min(sar, std::min(s[i - 1].low(), s[i - 2].low()));
            if (s[i].low() < sar) {           // reversal to downtrend
                up = false;
                sar = ep;
                ep = s[i].low();
                af = afStart;
            } else if (s[i].high() > ep) {
                ep = s[i].high();
                af = std::min(af + afStep, afMax);
            }
        } else {
            sar = std::max(sar, std::max(s[i - 1].high(), s[i - 2].high()));
            if (s[i].high() > sar) {          // reversal to uptrend
                up = true;
                sar = ep;
                ep = s[i].high();
                af = afStart;
            } else if (s[i].low() < ep) {
                ep = s[i].low();
                af = std::min(af + afStep, afMax);
            }
        }
        out[i] = sar;
    }
    return out;
}

// ------------------------------------------------------------------
// Rolling helpers
// ------------------------------------------------------------------

std::vector<double> Indicators::highest(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        double m = -std::numeric_limits<double>::infinity();
        for (int j = i - period + 1; j <= i; j++) {
            m = std::max(m, v[j]);
        }
        out[i] = m;
    }
    return out;
}

std::vector<double> Indicators::lowest(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        double m = std::numeric_limits<double>::infinity();
        for (int j = i - period + 1; j <= i; j++) {
            m = std::min(m, v[j]);
        }
        out[i] = m;
    }
    return out;
}

std::vector<double> Indicators::rollingStd(const std::vector<double>& v, int period) {
    checkPeriod(period);
    int n = static_cast<int>(v.size());
    std::vector<double> out = math_utils::nanArray(n);
    for (int i = period - 1; i < n; i++) {
        out[i] = math_utils::stdDevP(v, i - period + 1, i + 1);
    }
    return out;
}

}  // namespace quantfinlib
