#include "quantfinlib/risk/covariance_shrinkage.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <string>

namespace quantfinlib {
namespace covariance_shrinkage {

Result ledoitWolf(const Matrix& returns) {
    int t = static_cast<int>(returns.size());
    if (t < 2) {
        throw std::invalid_argument("need >= 2 observations, got " + std::to_string(t));
    }
    int n = static_cast<int>(returns[0].size());
    if (n < 1) {
        throw std::invalid_argument("need >= 1 asset");
    }
    for (int i = 0; i < t; i++) {
        if (static_cast<int>(returns[i].size()) != n) {
            throw std::invalid_argument("ragged matrix: row " + std::to_string(i));
        }
        for (int j = 0; j < n; j++) {
            if (!std::isfinite(returns[i][j])) {
                throw std::invalid_argument("non-finite return at "
                        + std::to_string(i) + "," + std::to_string(j));
            }
        }
    }
    std::vector<double> mean(n, 0.0);
    for (const auto& row : returns) {
        for (int j = 0; j < n; j++) {
            mean[j] += row[j];
        }
    }
    for (int j = 0; j < n; j++) {
        mean[j] /= t;
    }
    // Demeaned data and the sample covariance S (population, /T — the
    // Ledoit-Wolf convention).
    Matrix x(t, std::vector<double>(n));
    for (int i = 0; i < t; i++) {
        for (int j = 0; j < n; j++) {
            x[i][j] = returns[i][j] - mean[j];
        }
    }
    Matrix s(n, std::vector<double>(n, 0.0));
    for (int a = 0; a < n; a++) {
        for (int b = a; b < n; b++) {
            double sum = 0;
            for (int i = 0; i < t; i++) {
                sum += x[i][a] * x[i][b];
            }
            s[a][b] = sum / t;
            s[b][a] = s[a][b];
        }
    }
    double mu = 0;
    for (int a = 0; a < n; a++) {
        mu += s[a][a];
    }
    mu /= n;

    // d^2 = ||S - mu I||_F^2 / n   (LW normalize by n; cancels in the ratio
    // as long as both d2 and b2 use the same normalization).
    double d2 = 0;
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            double target = a == b ? mu : 0;
            double e = s[a][b] - target;
            d2 += e * e;
        }
    }
    d2 /= n;

    // b^2 = (1/T^2) sum_t ||x_t x_t' - S||_F^2 / n, clamped to d^2.
    double b2 = 0;
    for (int i = 0; i < t; i++) {
        double norm = 0;
        for (int a = 0; a < n; a++) {
            for (int b = 0; b < n; b++) {
                double e = x[i][a] * x[i][b] - s[a][b];
                norm += e * e;
            }
        }
        b2 += norm;
    }
    b2 /= (static_cast<double>(t) * t * n);
    b2 = std::min(b2, d2);

    double delta = d2 > 0 ? b2 / d2 : 1.0;  // degenerate S == mu I: any delta identical
    Matrix shrunk(n, std::vector<double>(n));
    for (int a = 0; a < n; a++) {
        for (int b = 0; b < n; b++) {
            double target = a == b ? mu : 0;
            shrunk[a][b] = delta * target + (1 - delta) * s[a][b];
        }
    }
    return {std::move(shrunk), delta, mu};
}

Matrix shrink(const Matrix& returns) {
    return ledoitWolf(returns).matrix;
}

}  // namespace covariance_shrinkage
}  // namespace quantfinlib
