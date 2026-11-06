#include "quantfinlib/volatility/garch11.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {

namespace {

using Vec = std::vector<double>;
constexpr double kNegInf = -std::numeric_limits<double>::infinity();
constexpr double kNuMin = 2.05;

double logistic(double u) { return 1 / (1 + std::exp(-u)); }
double logit(double p) { return std::log(p / (1 - p)); }

struct Model {
    double h0;
    bool studentT;

    int size() const { return studentT ? 4 : 3; }

    Vec toTheta(const Vec& u) const {
        double p = logistic(u[1]);
        double share = logistic(u[2]);
        Vec th = {std::exp(u[0]), p * share, p * (1 - share)};
        if (studentT) {
            th.push_back(kNuMin + std::exp(u[3]));
        }
        return th;
    }

    Vec toU(const Vec& th) const {
        double p = th[1] + th[2];
        Vec u = {std::log(th[0]), logit(p), logit(th[1] / p)};
        if (studentT) {
            u.push_back(std::log(th[3] - kNuMin));
        }
        return u;
    }

    double logLik(const Vec& th, const Vec& r, Vec* perObs) const {
        double omega = th[0], alpha = th[1], beta = th[2];
        double nu = studentT ? th[3] : 0;
        if (!(omega > 0) || !(alpha >= 0) || !(beta >= 0) || (studentT && !(nu > 2))) {
            return kNegInf;
        }
        double c = studentT ? math_utils::logGamma(0.5 * (nu + 1)) - math_utils::logGamma(0.5 * nu)
                        - 0.5 * std::log(std::numbers::pi * (nu - 2))
                            : 0;
        double h = h0;
        double ll = 0;
        for (std::size_t i = 0; i < r.size(); i++) {
            double x = r[i];
            double li = studentT
                    ? c - 0.5 * std::log(h) - 0.5 * (nu + 1) * std::log1p(x * x / (h * (nu - 2)))
                    : -0.5 * (std::log(2 * std::numbers::pi) + std::log(h) + x * x / h);
            if (perObs != nullptr) {
                (*perObs)[i] = li;
            }
            ll += li;
            h = omega + alpha * x * x + beta * h;
            if (!(h > 0) || !std::isfinite(h)) {
                return kNegInf;
            }
        }
        return std::isfinite(ll) ? ll : kNegInf;
    }
};

// Nelder-Mead with the standard coefficients, restarted from the best vertex
// until a restart stops improving: one simplex can collapse early in a valley.
template <typename F>
Vec nelderMead(F f, Vec x0, double step, double tol) {
    int n = static_cast<int>(x0.size());
    double fBest = f(x0);
    for (int restart = 0; restart < 20; restart++) {
        std::vector<Vec> v(n + 1, x0);
        Vec fv(n + 1);
        for (int i = 0; i < n; i++) {
            v[i + 1][i] += step;
        }
        for (int i = 0; i <= n; i++) {
            fv[i] = f(v[i]);
        }
        for (int iter = 0; iter < 20000; iter++) {
            std::vector<int> o(n + 1);
            for (int i = 0; i <= n; i++) {
                o[i] = i;
            }
            std::sort(o.begin(), o.end(), [&](int a, int b) { return fv[a] < fv[b]; });
            int best = o[0], worst = o[n], second = o[n - 1];
            if (std::fabs(fv[worst] - fv[best]) <= tol * (std::fabs(fv[best]) + 1e-300)) {
                break;
            }
            Vec c(n, 0.0);
            for (int i = 0; i <= n; i++) {
                if (i != worst) {
                    for (int j = 0; j < n; j++) {
                        c[j] += v[i][j] / n;
                    }
                }
            }
            auto along = [&](double t) {
                Vec p(n);
                for (int j = 0; j < n; j++) {
                    p[j] = c[j] + t * (v[worst][j] - c[j]);
                }
                return p;
            };
            Vec xr = along(-1);
            double fr = f(xr);
            if (fr < fv[best]) {
                Vec xe = along(-2);
                double fe = f(xe);
                v[worst] = fe < fr ? xe : xr;
                fv[worst] = std::min(fe, fr);
            } else if (fr < fv[second]) {
                v[worst] = xr;
                fv[worst] = fr;
            } else {
                Vec xc = along(fr < fv[worst] ? -0.5 : 0.5);
                double fc = f(xc);
                if (fc < std::min(fr, fv[worst])) {
                    v[worst] = xc;
                    fv[worst] = fc;
                } else {
                    for (int i = 0; i <= n; i++) {
                        if (i != best) {
                            for (int j = 0; j < n; j++) {
                                v[i][j] = v[best][j] + 0.5 * (v[i][j] - v[best][j]);
                            }
                            fv[i] = f(v[i]);
                        }
                    }
                }
            }
        }
        int b = static_cast<int>(std::min_element(fv.begin(), fv.end()) - fv.begin());
        bool stalled = !(fv[b] < fBest - 1e-10 * std::fabs(fBest));
        if (fv[b] < fBest) {
            fBest = fv[b];
            x0 = v[b];
        }
        if (stalled && restart > 0) {
            break;
        }
    }
    return x0;
}

Vec shifted(const Vec& th, int i, double di, int j, double dj) {
    Vec t = th;
    t[i] += di;
    if (j >= 0) {
        t[j] += dj;
    }
    return t;
}

void standardErrors(const Model& m, const Vec& th, const Vec& r, Vec& se, Vec& robust) {
    int k = static_cast<int>(th.size());
    int n = static_cast<int>(r.size());
    se.assign(k, std::numeric_limits<double>::quiet_NaN());
    robust = se;
    Vec h(k);
    for (int i = 0; i < k; i++) {
        h[i] = 1e-4 * std::max(std::fabs(th[i]), 1e-4);
    }
    double f0 = m.logLik(th, r, nullptr);
    math_utils::Matrix info(k, Vec(k));
    for (int i = 0; i < k; i++) {
        for (int j = i; j < k; j++) {
            double v;
            if (i == j) {
                v = (m.logLik(shifted(th, i, h[i], -1, 0), r, nullptr) - 2 * f0
                     + m.logLik(shifted(th, i, -h[i], -1, 0), r, nullptr)) / (h[i] * h[i]);
            } else {
                v = (m.logLik(shifted(th, i, h[i], j, h[j]), r, nullptr)
                     - m.logLik(shifted(th, i, h[i], j, -h[j]), r, nullptr)
                     - m.logLik(shifted(th, i, -h[i], j, h[j]), r, nullptr)
                     + m.logLik(shifted(th, i, -h[i], j, -h[j]), r, nullptr))
                        / (4 * h[i] * h[j]);
            }
            if (!std::isfinite(v)) {
                return;  // an estimate on the boundary has no two-sided Hessian
            }
            info[i][j] = info[j][i] = -v;
        }
    }
    math_utils::Matrix inv;
    try {
        math_utils::choleskyStrict(info);
        inv = math_utils::inverse(info);
    } catch (const std::invalid_argument&) {
        return;
    }
    math_utils::Matrix scores(k, Vec(n));
    Vec plus(n), minus(n);
    for (int i = 0; i < k; i++) {
        m.logLik(shifted(th, i, h[i], -1, 0), r, &plus);
        m.logLik(shifted(th, i, -h[i], -1, 0), r, &minus);
        for (int t = 0; t < n; t++) {
            scores[i][t] = (plus[t] - minus[t]) / (2 * h[i]);
        }
    }
    for (int a = 0; a < k; a++) {
        if (inv[a][a] > 0) {
            se[a] = std::sqrt(inv[a][a]);
        }
        double s = 0;
        for (int t = 0; t < n; t++) {
            double d = 0;
            for (int p = 0; p < k; p++) {
                d += inv[a][p] * scores[p][t];
            }
            s += d * d;
        }
        if (s > 0) {
            robust[a] = std::sqrt(s);
        }
    }
}

}  // namespace

Garch11::Params Garch11::fit(const std::vector<double>& returns) {
    return estimate(returns, Innovation::GAUSSIAN).params;
}

Garch11::Fit Garch11::estimate(const std::vector<double>& returns, Innovation innovation) {
    if (returns.size() < 100) {
        throw std::invalid_argument("need at least 100 returns, got "
                                    + std::to_string(returns.size()));
    }
    for (double x : returns) {
        if (!std::isfinite(x)) {
            throw std::invalid_argument("returns must be finite");
        }
    }
    double mean = math_utils::mean(returns);
    Vec r(returns.size());
    for (std::size_t i = 0; i < r.size(); i++) {
        r[i] = returns[i] - mean;
    }
    double var = math_utils::variance(r);
    if (!(var > 0)) {
        throw std::invalid_argument("returns carry no variance");
    }
    bool t = innovation == Innovation::STUDENT_T;
    Model model{var, t};
    auto objective = [&](const Vec& u) {
        double ll = model.logLik(model.toTheta(u), r, nullptr);
        return ll == kNegInf ? std::numeric_limits<double>::infinity() : -ll;
    };
    Vec bestU;
    double bestF = std::numeric_limits<double>::infinity();
    const double starts[][2] = {{0.05, 0.90}, {0.10, 0.60}, {0.03, 0.96}};
    for (const auto& s : starts) {
        Vec th = {var * (1 - s[0] - s[1]), s[0], s[1]};
        if (t) {
            th.push_back(8);
        }
        Vec u = nelderMead(objective, model.toU(th), 0.5, 1e-12);
        double f = objective(u);
        if (f < bestF) {
            bestF = f;
            bestU = u;
        }
    }
    if (bestU.empty()) {
        throw std::invalid_argument("likelihood is not finite at any starting point");
    }
    Vec th = model.toTheta(bestU);
    Fit fit{{th[0], th[1], th[2], -bestF}, innovation,
            t ? th[3] : std::numeric_limits<double>::quiet_NaN(), {}, {}};
    standardErrors(model, th, r, fit.standardErrors, fit.robustStandardErrors);
    return fit;
}

std::vector<double> Garch11::conditionalVariances(const std::vector<double>& returns,
                                                  const Params& params) {
    double mean = math_utils::mean(returns);
    std::vector<double> h(returns.size());
    h[0] = math_utils::variance(returns);
    for (std::size_t t = 1; t < returns.size(); t++) {
        double x = returns[t - 1] - mean;
        h[t] = params.omega + params.alpha * x * x + params.beta * h[t - 1];
    }
    return h;
}

double Garch11::forecastVariance(const std::vector<double>& returns,
                                 const Params& params, int horizon) {
    if (horizon < 1) {
        throw std::invalid_argument("horizon must be >= 1");
    }
    double mean = math_utils::mean(returns);
    std::vector<double> h = conditionalVariances(returns, params);
    double lastR = returns.back() - mean;
    double next = params.omega + params.alpha * lastR * lastR + params.beta * h.back();
    double uncond = params.unconditionalVariance();
    return uncond + std::pow(params.persistence(), horizon - 1) * (next - uncond);
}

}  // namespace quantfinlib
