#include "quantfinlib/pricing/least_squares.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

#include "quantfinlib/util/math_utils.hpp"

namespace quantfinlib {
namespace least_squares {

namespace {

using Vec = std::vector<double>;

bool finite(const Vec& r) {
    return std::all_of(r.begin(), r.end(), [](double v) { return std::isfinite(v); });
}

std::optional<Vec> tryApply(const Residuals& f, const Vec& x) {
    try {
        Vec r = f(x);
        if (finite(r)) {
            return r;
        }
    } catch (const std::invalid_argument&) {
    }
    return std::nullopt;
}

double sumSq(const Vec& r) {
    double s = 0;
    for (double v : r) {
        s += v * v;
    }
    return s;
}

}  // namespace

Solution minimise(const Residuals& residuals, const Vec& start, int maxIterations) {
    int n = static_cast<int>(start.size());
    Vec p = start;
    Vec r = residuals(p);
    if (!finite(r)) {
        throw std::invalid_argument("non-finite residual at the starting point");
    }
    double cost = sumSq(r);
    double lambda = 1e-3;
    for (int iter = 0; iter < maxIterations; iter++) {
        int m = static_cast<int>(r.size());
        math_utils::Matrix jac(m, Vec(n));
        for (int j = 0; j < n; j++) {
            double h = 1e-7 * (1 + std::fabs(p[j]));
            Vec q = p;
            q[j] += h;
            std::optional<Vec> rq = tryApply(residuals, q);
            if (!rq) {
                q[j] = p[j] - h;
                h = -h;
                rq = tryApply(residuals, q);
                if (!rq) {
                    return {p, cost};
                }
            }
            for (int i = 0; i < m; i++) {
                jac[i][j] = ((*rq)[i] - r[i]) / h;
            }
        }
        math_utils::Matrix jtj(n, Vec(n, 0.0));
        Vec jtr(n, 0.0);
        for (int i = 0; i < m; i++) {
            for (int a = 0; a < n; a++) {
                jtr[a] += jac[i][a] * r[i];
                for (int b = a; b < n; b++) {
                    jtj[a][b] += jac[i][a] * jac[i][b];
                }
            }
        }
        for (int a = 0; a < n; a++) {
            for (int b = 0; b < a; b++) {
                jtj[a][b] = jtj[b][a];
            }
        }
        bool improved = false;
        while (lambda < 1e12) {
            math_utils::Matrix lhs = jtj;
            Vec rhs(n);
            for (int a = 0; a < n; a++) {
                lhs[a][a] += lambda * std::max(jtj[a][a], 1e-12);
                rhs[a] = -jtr[a];
            }
            Vec trial(n);
            std::optional<Vec> rt;
            try {
                Vec step = math_utils::solveLinear(lhs, rhs);
                for (int a = 0; a < n; a++) {
                    trial[a] = p[a] + step[a];
                }
                rt = tryApply(residuals, trial);
            } catch (const std::invalid_argument&) {
            }
            double ct = rt ? sumSq(*rt) : std::numeric_limits<double>::infinity();
            if (ct < cost) {
                double rel = (cost - ct) / std::max(cost, 1e-300);
                p = trial;
                r = *rt;
                cost = ct;
                lambda = std::max(lambda / 3, 1e-12);
                improved = true;
                if (rel < 1e-14) {
                    return {p, cost};
                }
                break;
            }
            lambda *= 4;
        }
        if (!improved || cost == 0) {
            break;
        }
    }
    return {p, cost};
}

}  // namespace least_squares
}  // namespace quantfinlib
