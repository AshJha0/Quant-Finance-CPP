// Port of com.quantfinlib.pricing.LeastSquares (Java): Levenberg-Marquardt
// with a forward-difference Jacobian and Marquardt diagonal damping. A
// residual function may throw std::invalid_argument or return non-finite
// values to mark a point infeasible; the step is then rejected.
#pragma once

#include <functional>
#include <vector>

namespace quantfinlib {
namespace least_squares {

struct Solution {
    std::vector<double> x;
    double cost;
};

using Residuals = std::function<std::vector<double>(const std::vector<double>&)>;

Solution minimise(const Residuals& residuals, const std::vector<double>& start,
                  int maxIterations);

}  // namespace least_squares
}  // namespace quantfinlib
