#include "quantfinlib/util/math_utils.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace math_utils {

namespace {

// Java's Math.log(2), computed once like the Java private static final LN2.
const double kLn2 = std::log(2.0);

// Java's Math.PI (M_PI is a non-standard extension; pin the same double).
constexpr double kPi = 3.141592653589793;

void swapPair(std::vector<double>& k, std::vector<int>& v, int a, int b) {
    double tk = k[a];
    k[a] = k[b];
    k[b] = tk;
    int tv = v[a];
    v[a] = v[b];
    v[b] = tv;
}

// Private recursive half of pairSort: quicksort with median-of-three
// pivots, recursing into the smaller half (O(log n) stack), insertion
// sort below a 12-element run. Copied 1:1 from the Java source.
void pairSortRange(std::vector<double>& k, std::vector<int>& v, int lo, int hi) {
    while (hi - lo > 12) {
        // Median-of-three pivot guards against sorted-input worst cases.
        int mid = static_cast<int>((static_cast<unsigned int>(lo) + static_cast<unsigned int>(hi)) >> 1);
        if (k[mid] < k[lo]) {
            swapPair(k, v, lo, mid);
        }
        if (k[hi] < k[lo]) {
            swapPair(k, v, lo, hi);
        }
        if (k[hi] < k[mid]) {
            swapPair(k, v, mid, hi);
        }
        double pivot = k[mid];
        int i = lo;
        int j = hi;
        while (i <= j) {
            while (k[i] < pivot) {
                i++;
            }
            while (k[j] > pivot) {
                j--;
            }
            if (i <= j) {
                swapPair(k, v, i, j);
                i++;
                j--;
            }
        }
        // Recurse into the smaller half, loop on the larger: O(log n) stack.
        if (j - lo < hi - i) {
            pairSortRange(k, v, lo, j);
            lo = i;
        } else {
            pairSortRange(k, v, i, hi);
            hi = j;
        }
    }
    for (int i = lo + 1; i <= hi; i++) {  // insertion sort for small runs
        double key = k[i];
        int val = v[i];
        int j = i - 1;
        while (j >= lo && k[j] > key) {
            k[j + 1] = k[j];
            v[j + 1] = v[j];
            j--;
        }
        k[j + 1] = key;
        v[j + 1] = val;
    }
}

double betaContinuedFraction(double a, double b, double x) {
    const double eps = 3e-14;
    const double tiny = 1e-300;
    double qab = a + b;
    double qap = a + 1;
    double qam = a - 1;
    double c = 1;
    double d = 1 - qab * x / qap;
    if (std::fabs(d) < tiny) {
        d = tiny;
    }
    d = 1 / d;
    double h = d;
    for (int m = 1; m <= 300; m++) {
        int m2 = 2 * m;
        double aa = m * (b - m) * x / ((qam + m2) * (a + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < tiny) {
            d = tiny;
        }
        c = 1 + aa / c;
        if (std::fabs(c) < tiny) {
            c = tiny;
        }
        d = 1 / d;
        h *= d * c;
        aa = -(a + m) * (qab + m) * x / ((a + m2) * (qap + m2));
        d = 1 + aa * d;
        if (std::fabs(d) < tiny) {
            d = tiny;
        }
        c = 1 + aa / c;
        if (std::fabs(c) < tiny) {
            c = tiny;
        }
        d = 1 / d;
        double del = d * c;
        h *= del;
        if (std::fabs(del - 1) < eps) {
            break;
        }
    }
    return h;
}

}  // namespace

double mean(const std::vector<double>& v) {
    double s = 0;
    for (double x : v) {
        s += x;
    }
    // Empty input yields 0/0 = NaN, exactly like Java's s / v.length.
    return s / static_cast<double>(v.size());
}

double mean(const std::vector<double>& v, int from, int to) {
    double s = 0;
    for (int i = from; i < to; i++) {
        s += v[i];
    }
    return s / (to - from);
}

double variance(const std::vector<double>& v) {
    if (v.size() < 2) {
        return 0;
    }
    double m = mean(v), s = 0;
    for (double x : v) {
        double d = x - m;
        s += d * d;
    }
    return s / (static_cast<double>(v.size()) - 1);
}

double stdDev(const std::vector<double>& v) {
    return std::sqrt(variance(v));
}

double stdDevP(const std::vector<double>& v, int from, int to) {
    int n = to - from;
    if (n < 1) {
        return 0;
    }
    double m = mean(v, from, to), s = 0;
    for (int i = from; i < to; i++) {
        double d = v[i] - m;
        s += d * d;
    }
    return std::sqrt(s / n);
}

double stdDevSample(const std::vector<double>& v, int from, int to) {
    int n = to - from;
    if (n < 2) {
        return 0;
    }
    double m = mean(v, from, to), s = 0;
    for (int i = from; i < to; i++) {
        double d = v[i] - m;
        s += d * d;
    }
    return std::sqrt(s / (n - 1));
}

double percentile(const std::vector<double>& values, double p) {
    std::vector<double> sorted = values;
    std::sort(sorted.begin(), sorted.end());
    return percentileSorted(sorted, p);
}

double percentileSorted(const std::vector<double>& sorted, double p) {
    if (sorted.empty()) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    double idx = p * (static_cast<double>(sorted.size()) - 1);
    int lo = static_cast<int>(std::floor(idx));
    int hi = static_cast<int>(std::ceil(idx));
    if (lo == hi) {
        return sorted[lo];
    }
    double frac = idx - lo;
    return sorted[lo] * (1 - frac) + sorted[hi] * frac;
}

double dot(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        // Localize the error here rather than truncating silently or
        // failing three frames deep in a risk calculation.
        throw std::invalid_argument("length mismatch: " + std::to_string(a.size())
                                    + " vs " + std::to_string(b.size()));
    }
    double s = 0;
    for (std::size_t i = 0; i < a.size(); i++) {
        s += a[i] * b[i];
    }
    return s;
}

std::vector<double> matVec(const Matrix& m, const std::vector<double>& v) {
    std::vector<double> out(m.size());
    for (std::size_t i = 0; i < m.size(); i++) {
        out[i] = dot(m[i], v);
    }
    return out;
}

double quadraticForm(const std::vector<double>& w, const Matrix& m) {
    return dot(w, matVec(m, w));
}

double covariance(const std::vector<double>& a, const std::vector<double>& b) {
    if (a.size() != b.size()) {
        throw std::invalid_argument("length mismatch: " + std::to_string(a.size())
                                    + " vs " + std::to_string(b.size()));
    }
    if (a.size() < 2) {
        throw std::invalid_argument("need at least 2 observations, got "
                                    + std::to_string(a.size()));
    }
    double ma = mean(a), mb = mean(b), s = 0;
    for (std::size_t i = 0; i < a.size(); i++) {
        s += (a[i] - ma) * (b[i] - mb);
    }
    return s / (static_cast<double>(a.size()) - 1);
}

double correlation(const std::vector<double>& a, const std::vector<double>& b) {
    double sa = stdDev(a), sb = stdDev(b);
    if (sa == 0 || sb == 0) {
        return 0;
    }
    return covariance(a, b) / (sa * sb);
}

Matrix cholesky(const Matrix& a) {
    int n = static_cast<int>(a.size());
    double maxDiag = 0;
    for (int i = 0; i < n; i++) {
        maxDiag = std::max(maxDiag, std::fabs(a[i][i]));
    }
    // Java's Double.MIN_NORMAL is the smallest positive normal double,
    // which is std::numeric_limits<double>::min() in C++.
    double indefinite = -1e-8 * std::max(maxDiag, std::numeric_limits<double>::min());
    Matrix l(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j <= i; j++) {
            double sum = a[i][j];
            for (int k = 0; k < j; k++) {
                sum -= l[i][k] * l[j][k];
            }
            if (i == j) {
                if (sum < indefinite) {
                    throw std::invalid_argument(
                            "matrix is not positive semi-definite (pivot "
                            + std::to_string(i) + " = " + std::to_string(sum)
                            + ") - check for correlations beyond 1");
                }
                if (sum <= 0) {
                    sum = 1e-12;
                }
                l[i][i] = std::sqrt(sum);
            } else {
                l[i][j] = sum / l[j][j];
            }
        }
    }
    return l;
}

double normInv(double p) {
    if (p <= 0 || p >= 1) {
        throw std::invalid_argument("p must be in (0,1): " + std::to_string(p));
    }
    // Acklam coefficients, copied digit-for-digit from the Java source.
    static const double a[] = {-3.969683028665376e+01, 2.209460984245205e+02, -2.759285104469687e+02,
                               1.383577518672690e+02, -3.066479806614716e+01, 2.506628277459239e+00};
    static const double b[] = {-5.447609879822406e+01, 1.615858368580409e+02, -1.556989798598866e+02,
                               6.680131188771972e+01, -1.328068155288572e+01};
    static const double c[] = {-7.784894002430293e-03, -3.223964580411365e-01, -2.400758277161838e+00,
                               -2.549732539343734e+00, 4.374664141464968e+00, 2.938163982698783e+00};
    static const double d[] = {7.784695709041462e-03, 3.224671290700398e-01, 2.445134137142996e+00,
                               3.754408661907416e+00};
    const double plow = 0.02425, phigh = 1 - plow;
    double q, r;
    if (p < plow) {
        q = std::sqrt(-2 * std::log(p));
        return (((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
                / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    if (p > phigh) {
        q = std::sqrt(-2 * std::log(1 - p));
        return -(((((c[0] * q + c[1]) * q + c[2]) * q + c[3]) * q + c[4]) * q + c[5])
                / ((((d[0] * q + d[1]) * q + d[2]) * q + d[3]) * q + 1);
    }
    q = p - 0.5;
    r = q * q;
    return (((((a[0] * r + a[1]) * r + a[2]) * r + a[3]) * r + a[4]) * r + a[5]) * q
            / (((((b[0] * r + b[1]) * r + b[2]) * r + b[3]) * r + b[4]) * r + 1);
}

double skewness(const std::vector<double>& v) {
    double m = mean(v);
    double m2 = 0, m3 = 0;
    for (double x : v) {
        double d = x - m;
        m2 += d * d;
        m3 += d * d * d;
    }
    m2 /= static_cast<double>(v.size());
    m3 /= static_cast<double>(v.size());
    return m2 == 0 ? 0 : m3 / std::pow(m2, 1.5);
}

double kurtosis(const std::vector<double>& v) {
    double m = mean(v);
    double m2 = 0, m4 = 0;
    for (double x : v) {
        double d = x - m;
        m2 += d * d;
        m4 += d * d * d * d;
    }
    m2 /= static_cast<double>(v.size());
    m4 /= static_cast<double>(v.size());
    return m2 == 0 ? 0 : m4 / (m2 * m2);
}

void pairSort(std::vector<double>& keys, std::vector<int>& values) {
    if (keys.size() != values.size()) {
        throw std::invalid_argument("keys and values must align");
    }
    pairSortRange(keys, values, 0, static_cast<int>(keys.size()) - 1);
}

double normPdf(double x) {
    return std::exp(-0.5 * x * x) / std::sqrt(2 * kPi);
}

double normCdf(double x) {
    if (x < 0) {
        return 1 - normCdf(-x);
    }
    double t = 1 / (1 + 0.2316419 * x);
    double poly = t * (0.319381530 + t * (-0.356563782 + t * (1.781477937
            + t * (-1.821255978 + t * 1.330274429))));
    return 1 - normPdf(x) * poly;
}

std::vector<double> solveLinear(const Matrix& a, const std::vector<double>& b) {
    int n = static_cast<int>(b.size());
    Matrix m(n);
    std::vector<double> rhs = b;
    for (int i = 0; i < n; i++) {
        m[i] = a[i];
    }
    for (int col = 0; col < n; col++) {
        int pivot = col;
        for (int r = col + 1; r < n; r++) {
            if (std::fabs(m[r][col]) > std::fabs(m[pivot][col])) {
                pivot = r;
            }
        }
        if (std::fabs(m[pivot][col]) < 1e-12) {
            throw std::invalid_argument("singular system at column " + std::to_string(col));
        }
        std::swap(m[col], m[pivot]);
        std::swap(rhs[col], rhs[pivot]);
        for (int r = col + 1; r < n; r++) {
            double f = m[r][col] / m[col][col];
            rhs[r] -= f * rhs[col];
            for (int c = col; c < n; c++) {
                m[r][c] -= f * m[col][c];
            }
        }
    }
    std::vector<double> x(n);
    for (int i = n - 1; i >= 0; i--) {
        double s = rhs[i];
        for (int j = i + 1; j < n; j++) {
            s -= m[i][j] * x[j];
        }
        x[i] = s / m[i][i];
    }
    return x;
}

Matrix inverse(const Matrix& a) {
    int n = static_cast<int>(a.size());
    Matrix aug(n, std::vector<double>(2 * n, 0.0));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            aug[i][j] = a[i][j];
        }
        aug[i][n + i] = 1;
    }
    for (int col = 0; col < n; col++) {
        int pivot = col;
        for (int r = col + 1; r < n; r++) {
            if (std::fabs(aug[r][col]) > std::fabs(aug[pivot][col])) {
                pivot = r;
            }
        }
        if (std::fabs(aug[pivot][col]) < 1e-12) {
            throw std::invalid_argument("singular matrix at column " + std::to_string(col));
        }
        std::swap(aug[col], aug[pivot]);
        double diag = aug[col][col];
        for (int c = 0; c < 2 * n; c++) {
            aug[col][c] /= diag;
        }
        for (int r = 0; r < n; r++) {
            if (r == col) {
                continue;
            }
            double f = aug[r][col];
            for (int c = 0; c < 2 * n; c++) {
                aug[r][c] -= f * aug[col][c];
            }
        }
    }
    Matrix inv(n, std::vector<double>(n, 0.0));
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < n; j++) {
            inv[i][j] = aug[i][n + j];
        }
    }
    return inv;
}

double clamp(double v, double lo, double hi) {
    return std::max(lo, std::min(hi, v));
}

std::vector<double> nanArray(int n) {
    return std::vector<double>(n, std::numeric_limits<double>::quiet_NaN());
}

double decayFactor(std::int64_t dtNanos, std::int64_t halfLifeNanos) {
    return dtNanos <= 0
            ? 1.0
            : std::exp(-static_cast<double>(dtNanos) * kLn2
                       / static_cast<double>(halfLifeNanos));
}

double logGamma(double x) {
    if (!(x > 0)) {  // NaN-rejecting gate, exactly the Java idiom
        throw std::invalid_argument("x must be > 0: " + std::to_string(x));
    }
    static const double cof[] = {76.18009172947146, -86.50532032941677, 24.01409824083091,
                                 -1.231739572450155, 0.1208650973866179e-2, -0.5395239384953e-5};
    double y = x;
    double tmp = x + 5.5;
    tmp -= (x + 0.5) * std::log(tmp);
    double ser = 1.000000000190015;
    for (double c : cof) {
        ser += c / ++y;
    }
    return -tmp + std::log(2.5066282746310005 * ser / x);
}

double regularizedIncompleteBeta(double a, double b, double x) {
    if (!(a > 0) || !(b > 0) || !(x >= 0 && x <= 1)) {
        throw std::invalid_argument("need a, b > 0 and x in [0, 1]");
    }
    if (x == 0 || x == 1) {
        return x;
    }
    double lnFront = logGamma(a + b) - logGamma(a) - logGamma(b)
            + a * std::log(x) + b * std::log(1 - x);
    if (x < (a + 1) / (a + b + 2)) {
        return std::exp(lnFront) * betaContinuedFraction(a, b, x) / a;
    }
    return 1 - std::exp(lnFront) * betaContinuedFraction(b, a, 1 - x) / b;
}

double tCdf(double t, double df) {
    if (!(df > 0)) {
        throw std::invalid_argument("df must be > 0: " + std::to_string(df));
    }
    if (t == 0) {
        return 0.5;
    }
    double halfTail = 0.5 * regularizedIncompleteBeta(df / 2, 0.5, df / (df + t * t));
    return t > 0 ? 1 - halfTail : halfTail;
}

}  // namespace math_utils
}  // namespace quantfinlib
